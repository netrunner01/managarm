#include <thor-internal/cpu-data.hpp>
#include <thor-internal/debug.hpp>
#include <thor-internal/timer.hpp>
#include <thor-internal/schedule.hpp>

namespace thor {

static constexpr bool logTimers = false;
static constexpr bool logProgress = false;


namespace {

struct DeadlineState {
	frg::optional<uint64_t> timerDeadline{};
	frg::optional<uint64_t> preemptionDeadline{};

	// Cache of what we last programmed into the timer hardware, used to skip redundant
	// reprogramming. `hwArmed` records whether the hardware still actually holds it.
	//
	// Timer IRQs are one shot -- in TSC-deadline mode the MSR is cleared by hardware when
	// it fires -- so the cache goes stale on every IRQ and handleTimerInterrupt() must
	// invalidate it. This matters because the deadline is programmed in a different clock
	// domain than getClockNanos() (see setTimerDeadline()), so an IRQ can arrive a hair
	// BEFORE getClockNanos() reaches the deadline. Such an IRQ clears no deadline, so the
	// recomputed deadline is unchanged; without invalidation updateDeadline_() would skip
	// setTimerDeadline() and leave the timer disarmed forever, wedging every timer on this
	// CPU permanently.
	frg::optional<uint64_t> currentDeadline{};
	bool hwArmed{false};
};

extern PerCpu<DeadlineState> deadlineState;
THOR_DEFINE_PERCPU(deadlineState);


void updateDeadline_() {
	assert(!intsAreEnabled());
	auto &state = deadlineState.get();

	frg::optional<uint64_t> deadline;
	auto consider = [&] (auto candidate) {
		if (!deadline)
			deadline = candidate;
		else if (candidate)
			deadline = frg::min(*deadline, *candidate);
	};

	consider(state.timerDeadline);
	consider(state.preemptionDeadline);

	// No need to do anything if the hardware is already in the state we want. Note that
	// this is gated on hwArmed: an unchanged deadline does NOT imply the hardware still
	// holds it, because one shot timers disarm themselves when they fire.

	// FIXME(qookie): This is just deadline == state.currentDeadline,
	// but frg::optional is missing the overload to do that.
	if (!deadline && !state.hwArmed)
		return;
	if (deadline && state.hwArmed && state.currentDeadline
			&& deadline == *state.currentDeadline)
		return;

	state.currentDeadline = deadline;
	state.hwArmed = static_cast<bool>(deadline);
	setTimerDeadline(state.currentDeadline);
}

void setTimerEngineDeadline(frg::optional<uint64_t> deadline) {
	assert(!intsAreEnabled());
	deadlineState.get().timerDeadline = deadline;
	updateDeadline_();
}

} // namespace anonymous


void setPreemptionDeadline(frg::optional<uint64_t> deadline) {
	assert(!intsAreEnabled());
	deadlineState.get().preemptionDeadline = deadline;
	updateDeadline_();
}

 frg::optional<uint64_t> getPreemptionDeadline() {
	assert(!intsAreEnabled());
	return deadlineState.get().preemptionDeadline;
}


void handleTimerInterrupt() {
	auto &state = deadlineState.get();
	auto now = getClockNanos();

	// Clear all deadlines that have expired.
	auto checkAndClear = [&](frg::optional<uint64_t> &deadline) -> bool {
		if (!deadline || now < *deadline)
			return false;
		deadline = frg::null_opt;
		return true;
	};

	auto timerExpired = checkAndClear(state.timerDeadline);
	auto preemptionExpired = checkAndClear(state.preemptionDeadline);

	// The one shot timer disarmed itself when it fired, so our cached view of the
	// hardware is now stale. Invalidate it before recomputing: otherwise an early IRQ
	// (which clears no deadline, hence recomputes an unchanged one) would skip
	// reprogramming and leave the timer disarmed forever.
	state.hwArmed = false;

	// Update the timer hardware.
	updateDeadline_();

	// Finally, take action for the deadlines that have expired.
	if (timerExpired)
		generalTimerEngine()->firedAlarm();

	if (preemptionExpired)
		localScheduler.get().forcePreemptionCall();
}


extern PerCpu<PrecisionTimerEngine> timerEngine;
THOR_DEFINE_PERCPU(timerEngine);

void PrecisionTimerEngine::installTimer(PrecisionTimerNode *timer) {
	assert(!timer->_engine);
	timer->_engine = this;

	auto irq_lock = frg::guard(&irqMutex());
	auto lock = frg::guard(&_mutex);
	assert(timer->_state == TimerState::none);

	if(logTimers) {
		auto current = getClockNanos();
		infoLogger() << "thor: Setting timer at " << timer->_deadline
				<< " (counter is " << current << ")" << frg::endlog;
	}

//	infoLogger() << "thor: Active timers: " << _activeTimers << frg::endlog;

	if(!timer->_cancelCb.try_set(timer->_cancelToken)) {
		timer->_wasCancelled = true;
		timer->_state = TimerState::retired;
		timer->_wq->post(timer->_elapsed);
		return;
	}

	_timerQueue.push(timer);
	_activeTimers++;
	timer->_state = TimerState::queued;

	_progress();
}

void PrecisionTimerEngine::cancelTimer(PrecisionTimerNode *timer) {
	auto irq_lock = frg::guard(&irqMutex());
	auto lock = frg::guard(&_mutex);

	if(timer->_state == TimerState::queued) {
		_timerQueue.remove(timer);
		_activeTimers--;
		timer->_wasCancelled = true;
	}else{
		assert(timer->_state == TimerState::elapsed);
	}

	timer->_state = TimerState::retired;
	timer->_wq->post(timer->_elapsed);
}

void PrecisionTimerEngine::firedAlarm() {
	assert(getCpuData() == _ourCpu);

	auto irq_lock = frg::guard(&irqMutex());
	auto lock = frg::guard(&_mutex);

	_progress();
}

// This function unconditionally calls into setTimerEngineDeadline().
// This is necessary since we assume that timer IRQs are one shot
// and not necessarily perfectly accurate.
void PrecisionTimerEngine::_progress() {
	assert(getCpuData() == _ourCpu);

	auto current = getClockNanos();
	do {
		// Process all timers that elapsed in the past.
		if(logProgress)
			infoLogger() << "thor: Processing timers until " << current << frg::endlog;
		while(true) {
			if(_timerQueue.empty()) {
				setTimerEngineDeadline(frg::null_opt);
				return;
			}

			if(_timerQueue.top()->_deadline > current)
				break;

			auto timer = _timerQueue.top();
			assert(timer->_state == TimerState::queued);
			_timerQueue.pop();
			_activeTimers--;
			if(logProgress)
				infoLogger() << "thor: Timer completed" << frg::endlog;
			if(timer->_cancelCb.try_reset()) {
				timer->_state = TimerState::retired;
				timer->_wq->post(timer->_elapsed);
			}else{
				// Let the cancellation handler invoke the continuation.
				timer->_state = TimerState::elapsed;
			}
		}

		// Setup the interrupt.
		assert(!_timerQueue.empty());
		setTimerEngineDeadline(_timerQueue.top()->_deadline);

		// We iterate if there was a race.
		// Technically, this is optional but it may help to avoid unnecessary IRQs.
		current = getClockNanos();
	} while(_timerQueue.top()->_deadline <= current);
}

PrecisionTimerEngine *generalTimerEngine() {
	return &timerEngine.get();
}

} // namespace thor
