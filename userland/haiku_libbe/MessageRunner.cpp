/*
 * SPDX-License-Identifier: MIT
 *
 * Registrar-free BMessageRunner for Haiku-on-Linux.
 *
 * Upstream Haiku's BMessageRunner (src/kits/app/MessageRunner.cpp) registers
 * every runner with the registrar server, which then delivers the messages.
 * This port runs without a registrar (RUN_WITHOUT_REGISTRAR), so upstream
 * runners silently fail -- which, among other things, breaks
 * BWindow::SetPulseRate() and therefore every Pulse()-driven UI element
 * (e.g. the Deskbar clock). This implementation keeps the public API/ABI of
 * <MessageRunner.h> but delivers messages from a single in-process timer
 * thread.
 */

#include <MessageRunner.h>

#include <Application.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>

#include <condition_variable>
#include <map>
#include <mutex>
#include <new>
#include <thread>


namespace {

struct RunnerEntry {
	BMessenger	target;
	BMessenger	replyTo;
	BMessage	message;
	bigtime_t	interval;
	int32		count;		// < 0: infinite
	bigtime_t	nextTime;
	bool		detached;
};


class RunnerManager {
public:
	static RunnerManager& Get()
	{
		// Intentionally leaked: runners may be destroyed during static
		// destruction of other objects.
		static RunnerManager* sManager = new RunnerManager();
		return *sManager;
	}

	int32 Register(const BMessenger& target, const BMessage* message,
		bigtime_t interval, int32 count, bool detach, const BMessenger& replyTo)
	{
		if (message == NULL || count == 0 || interval <= 0)
			return B_BAD_VALUE;
		if (!target.IsValid())
			return B_BAD_VALUE;

		std::unique_lock<std::mutex> lock(fLock);
		_EnsureThread();
		int32 token = fNextToken++;
		RunnerEntry& entry = fEntries[token];
		entry.target = target;
		entry.replyTo = replyTo;
		entry.message = *message;
		entry.interval = interval;
		entry.count = count;
		entry.nextTime = system_time() + interval;
		entry.detached = detach;
		fCondition.notify_all();
		return token;
	}

	void Unregister(int32 token)
	{
		std::unique_lock<std::mutex> lock(fLock);
		fEntries.erase(token);
		fCondition.notify_all();
	}

	status_t SetParams(int32 token, bool resetInterval, bigtime_t interval,
		bool resetCount, int32 count)
	{
		std::unique_lock<std::mutex> lock(fLock);
		auto it = fEntries.find(token);
		if (it == fEntries.end())
			return B_BAD_VALUE;
		if (resetInterval) {
			if (interval <= 0)
				return B_BAD_VALUE;
			it->second.interval = interval;
			it->second.nextTime = system_time() + interval;
		}
		if (resetCount) {
			it->second.count = count;
			if (count == 0)
				fEntries.erase(it);
		}
		fCondition.notify_all();
		return B_OK;
	}

	status_t GetInfo(int32 token, bigtime_t* interval, int32* count)
	{
		std::unique_lock<std::mutex> lock(fLock);
		auto it = fEntries.find(token);
		if (it == fEntries.end())
			return B_BAD_VALUE;
		if (interval != NULL)
			*interval = it->second.interval;
		if (count != NULL)
			*count = it->second.count;
		return B_OK;
	}

private:
	RunnerManager() : fNextToken(1), fThreadStarted(false) {}

	void _EnsureThread()
	{
		if (fThreadStarted)
			return;
		fThreadStarted = true;
		std::thread([this]() { _Loop(); }).detach();
	}

	void _Loop()
	{
		std::unique_lock<std::mutex> lock(fLock);
		while (true) {
			if (fEntries.empty()) {
				fCondition.wait(lock);
				continue;
			}

			bigtime_t now = system_time();
			bigtime_t earliest = B_INFINITE_TIMEOUT;
			int32 dueToken = -1;
			for (auto& [token, entry] : fEntries) {
				if (entry.nextTime < earliest) {
					earliest = entry.nextTime;
					dueToken = token;
				}
			}

			if (earliest > now) {
				fCondition.wait_for(lock,
					std::chrono::microseconds(earliest - now));
				continue;
			}

			RunnerEntry& entry = fEntries[dueToken];
			BMessenger target = entry.target;
			BMessenger replyTo = entry.replyTo;
			BMessage message(entry.message);
			bool unlimited = entry.count < 0;
			if (!unlimited)
				entry.count--;
			entry.nextTime = now + entry.interval;
			if (!unlimited && entry.count <= 0)
				fEntries.erase(dueToken);

			// Deliver without holding the lock; never block the timer thread.
			lock.unlock();
			status_t status = target.SendMessage(&message, replyTo, 0);
			lock.lock();

			if (status == B_BAD_PORT_ID || status == B_BAD_VALUE) {
				// Target is gone; drop the runner.
				fEntries.erase(dueToken);
			}
		}
	}

	std::mutex					fLock;
	std::condition_variable		fCondition;
	std::map<int32, RunnerEntry> fEntries;
	int32						fNextToken;
	bool						fThreadStarted;
};

}	// namespace


BMessageRunner::BMessageRunner(BMessenger target, const BMessage* message,
	bigtime_t interval, int32 count)
	:
	fToken(-1)
{
	_InitData(target, message, interval, count, be_app_messenger);
}


BMessageRunner::BMessageRunner(BMessenger target, const BMessage& message,
	bigtime_t interval, int32 count)
	:
	fToken(-1)
{
	_InitData(target, &message, interval, count, be_app_messenger);
}


BMessageRunner::BMessageRunner(BMessenger target, const BMessage* message,
	bigtime_t interval, int32 count, BMessenger replyTo)
	:
	fToken(-1)
{
	_InitData(target, message, interval, count, replyTo);
}


BMessageRunner::BMessageRunner(BMessenger target, const BMessage& message,
	bigtime_t interval, int32 count, BMessenger replyTo)
	:
	fToken(-1)
{
	_InitData(target, &message, interval, count, replyTo);
}


BMessageRunner::~BMessageRunner()
{
	if (fToken >= 0)
		RunnerManager::Get().Unregister(fToken);
}


status_t
BMessageRunner::InitCheck() const
{
	return fToken >= 0 ? B_OK : fToken;
}


status_t
BMessageRunner::SetInterval(bigtime_t interval)
{
	return _SetParams(true, interval, false, 0);
}


status_t
BMessageRunner::SetCount(int32 count)
{
	return _SetParams(false, 0, true, count);
}


status_t
BMessageRunner::GetInfo(bigtime_t* interval, int32* count) const
{
	if (fToken < 0)
		return B_BAD_VALUE;
	return RunnerManager::Get().GetInfo(fToken, interval, count);
}


/*static*/ status_t
BMessageRunner::StartSending(BMessenger target, const BMessage* message,
	bigtime_t interval, int32 count)
{
	int32 token = _RegisterRunner(target, message, interval, count, true,
		be_app_messenger);
	return token >= B_OK ? B_OK : token;
}


/*static*/ status_t
BMessageRunner::StartSending(BMessenger target, const BMessage* message,
	bigtime_t interval, int32 count, BMessenger replyTo)
{
	int32 token = _RegisterRunner(target, message, interval, count, true,
		replyTo);
	return token >= B_OK ? B_OK : token;
}


/*static*/ int32
BMessageRunner::_RegisterRunner(BMessenger target, const BMessage* message,
	bigtime_t interval, int32 count, bool detach, BMessenger replyTo)
{
	return RunnerManager::Get().Register(target, message, interval, count,
		detach, replyTo);
}


void
BMessageRunner::_InitData(BMessenger target, const BMessage* message,
	bigtime_t interval, int32 count, BMessenger replyTo)
{
	fToken = _RegisterRunner(target, message, interval, count, false, replyTo);
}


status_t
BMessageRunner::_SetParams(bool resetInterval, bigtime_t interval,
	bool resetCount, int32 count)
{
	if ((!resetInterval && !resetCount) || fToken < 0)
		return B_BAD_VALUE;
	return RunnerManager::Get().SetParams(fToken, resetInterval, interval,
		resetCount, count);
}


void BMessageRunner::_ReservedMessageRunner1() {}
void BMessageRunner::_ReservedMessageRunner2() {}
void BMessageRunner::_ReservedMessageRunner3() {}
void BMessageRunner::_ReservedMessageRunner4() {}
void BMessageRunner::_ReservedMessageRunner5() {}
void BMessageRunner::_ReservedMessageRunner6() {}
