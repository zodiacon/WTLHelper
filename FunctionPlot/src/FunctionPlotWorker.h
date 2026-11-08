#pragma once

// Sampling the heavy graphs (implicit curves and regions, surfaces, implicit 3D surfaces) on a thread of its own, so
// the control draws a coarse version at once and the full one when it's ready. Window-free: the control is told
// through a callback (on the worker's thread) and takes the results on its own.

#include "FunctionPlotGraphs.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

struct FplJob {
	int graph = -1;
	uint64_t generation = 0;		// the result's, to tell it's still wanted
	FplExpression expr;				// a copy: the worker reads nothing of the control's
	std::vector<double> slots;
	FplViewport view;
	FplSampleOptions options;
	bool mesh = false;				// 3D: an implicit surface's mesh over box, cells a side
	FplBox3 box;
	int cells = 48;
};

struct FplJobResult {
	int graph = -1;
	uint64_t generation = 0;
	bool mesh = false;
	FplGeometry geometry;
	FplMesh triangles;
	FplViewport view;
};

class FplWorker {
public:
	// ready: results wait to be taken (called on the worker's thread, once until they're taken)
	explicit FplWorker(std::function<void()> ready);
	~FplWorker();					// stops and waits for the job being done
	FplWorker(const FplWorker&) = delete;
	FplWorker& operator=(const FplWorker&) = delete;

	// A job; one waiting for the same graph is replaced (only the latest is wanted).
	void Submit(FplJob job);
	std::vector<FplJobResult> Take();
	void Cancel();					// the waiting jobs and results dropped (the graphs changed)
	static FplJobResult Run(const FplJob& job);	// what the thread does with a job (also synchronously)

private:
	void Loop();

	std::function<void()> m_Ready;
	std::mutex m_Lock;
	std::condition_variable m_Wake;
	std::deque<FplJob> m_Jobs;
	std::vector<FplJobResult> m_Results;
	bool m_Stop = false;
	bool m_Told = false;
	std::thread m_Thread;
};
