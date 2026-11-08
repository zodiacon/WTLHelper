// The sampling thread.

#include "FunctionPlotWorker.h"

FplWorker::FplWorker(std::function<void()> ready) : m_Ready(std::move(ready)) {
	m_Thread = std::thread([this] { Loop(); });
}

FplWorker::~FplWorker() {
	{
		std::lock_guard lock(m_Lock);
		m_Stop = true;
		m_Jobs.clear();
	}
	m_Wake.notify_all();
	if (m_Thread.joinable())
		m_Thread.join();
}

void FplWorker::Submit(FplJob job) {
	{
		std::lock_guard lock(m_Lock);
		for (auto& waiting : m_Jobs)
			if (waiting.graph == job.graph && waiting.mesh == job.mesh) {
				waiting = std::move(job);
				return;
			}
		m_Jobs.push_back(std::move(job));
	}
	m_Wake.notify_one();
}

std::vector<FplJobResult> FplWorker::Take() {
	std::lock_guard lock(m_Lock);
	m_Told = false;
	return std::move(m_Results);
}

void FplWorker::Cancel() {
	std::lock_guard lock(m_Lock);
	m_Jobs.clear();
	m_Results.clear();
}

FplJobResult FplWorker::Run(const FplJob& job) {
	FplJobResult r;
	r.graph = job.graph;
	r.generation = job.generation;
	r.mesh = job.mesh;
	r.view = job.view;
	if (job.mesh && job.expr.kind == FplKind::Surface) {
		std::vector<double> slots = job.slots;
		r.triangles = FplSurfaceMesh([&](double x, double y) {
			slots[FplSlotX] = x;
			slots[FplSlotY] = y;
			return job.expr.f.Eval(slots.data());
		}, job.box, 96);
	}
	else if (job.mesh) {
		std::vector<double> slots = job.slots;
		r.triangles = FplImplicitMesh([&](double x, double y, double z) {
			slots[FplSlotX] = x;
			slots[FplSlotY] = y;
			slots[FplSlotZ] = z;
			return job.expr.f.Eval(slots.data());
		}, job.box, job.cells);
	}
	else
		r.geometry = FplSample(job.expr, job.slots, job.view, job.options);
	return r;
}

void FplWorker::Loop() {
	for (;;) {
		FplJob job;
		{
			std::unique_lock lock(m_Lock);
			m_Wake.wait(lock, [this] { return m_Stop || !m_Jobs.empty(); });
			if (m_Stop)
				return;
			job = std::move(m_Jobs.front());
			m_Jobs.pop_front();
		}
		FplJobResult result = Run(job);
		bool tell = false;
		{
			std::lock_guard lock(m_Lock);
			if (m_Stop)
				return;
			// a later job for the same graph waiting: this result isn't wanted
			bool superseded = false;
			for (auto& waiting : m_Jobs)
				superseded = superseded || (waiting.graph == result.graph && waiting.mesh == result.mesh);
			if (superseded)
				continue;
			for (auto it = m_Results.begin(); it != m_Results.end();)
				it = it->graph == result.graph && it->mesh == result.mesh ? m_Results.erase(it) : it + 1;
			m_Results.push_back(std::move(result));
			tell = !m_Told;
			m_Told = true;
		}
		if (tell && m_Ready)
			m_Ready();
	}
}
