// NodeGraphLayout.cpp : automatic arrangement of the nodes of a graph.
//

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../include/NodeGraphLayout.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace NodeGraphCtrl {

	namespace {

		constexpr float Pi = 3.14159265f;

		struct LEdge {
			int    From, To;
			size_t Index;		// in the model's edges
		};

		// A graph (or one connected part of it) by index: the nodes are 0..Size()-1, X and Y are their centers.
		struct Graph {
			std::vector<float>  W, H, X, Y;
			std::vector<LEdge>  Edges;					// no self loops
			std::vector<std::vector<Point>> Bends;		// the waypoints of each of Edges
			std::vector<std::vector<int>>   Out, In;	// indices in Edges

			int Size() const { return (int)W.size(); }

			void Link() {
				Out.assign(Size(), {});
				In.assign(Size(), {});
				for (int e = 0; e < (int)Edges.size(); e++) {
					Out[Edges[e].From].push_back(e);
					In[Edges[e].To].push_back(e);
				}
				Bends.assign(Edges.size(), {});
			}

			float Extent(int v) const { return std::max(W[v], H[v]); }
		};

		struct Box {
			float MinX = FLT_MAX, MinY = FLT_MAX, MaxX = -FLT_MAX, MaxY = -FLT_MAX;
			void Add(float x, float y, float w = 0.0f, float h = 0.0f) {
				MinX = std::min(MinX, x - w * 0.5f); MaxX = std::max(MaxX, x + w * 0.5f);
				MinY = std::min(MinY, y - h * 0.5f); MaxY = std::max(MaxY, y + h * 0.5f);
			}
			float Width() const { return MaxX - MinX; }
			float Height() const { return MaxY - MinY; }
		};

		Box Bounds(const Graph& g) {
			Box b;
			for (int v = 0; v < g.Size(); v++)
				b.Add(g.X[v], g.Y[v], g.W[v], g.H[v]);
			for (const auto& bends : g.Bends)
				for (const auto& p : bends)
					b.Add(p.X, p.Y);
			return b;
		}

		void Translate(Graph& g, float dx, float dy) {
			for (int v = 0; v < g.Size(); v++) { g.X[v] += dx; g.Y[v] += dy; }
			for (auto& bends : g.Bends)
				for (auto& p : bends) { p.X += dx; p.Y += dy; }
		}

		// Isotonic regression with gaps: the positions x (in this order) closest to the wanted ones (least squares,
		// weighted) such that x[i] - x[i-1] >= gap[i]. Pool adjacent violators on y = x - (sum of the gaps so far).
		void PlaceInOrder(const std::vector<float>& want, const std::vector<float>& weight, const std::vector<float>& gap,
			std::vector<float>& x) {
			size_t m = want.size();
			std::vector<float> offset(m);
			for (size_t i = 1; i < m; i++)
				offset[i] = offset[i - 1] + gap[i];

			struct Block { double SumW, SumWY; size_t Count; double Mean() const { return SumWY / SumW; } };
			std::vector<Block> blocks;
			for (size_t i = 0; i < m; i++) {
				double w = std::max(weight[i], 1e-4f);
				blocks.push_back({ w, w * (want[i] - offset[i]), 1 });
				while (blocks.size() > 1 && blocks[blocks.size() - 2].Mean() > blocks.back().Mean()) {
					Block last = blocks.back();
					blocks.pop_back();
					blocks.back().SumW += last.SumW;
					blocks.back().SumWY += last.SumWY;
					blocks.back().Count += last.Count;
				}
			}
			x.resize(m);
			size_t i = 0;
			for (const auto& b : blocks)
				for (size_t k = 0; k < b.Count; k++, i++)
					x[i] = (float)b.Mean() + offset[i];
		}

		// ---- Layered (Sugiyama) ----------------------------------------------------------------------------------

		void Layered(Graph& g, const LayoutOptions& o) {
			const int n = g.Size();
			const int edgeCount = (int)g.Edges.size();

			// 1. Break the cycles: an edge that goes back to a node on the DFS stack is turned around.
			// Starting from the sources keeps the natural direction of most edges.
			std::vector<char> reversed(edgeCount);
			{
				std::vector<int> starts;
				for (int v = 0; v < n; v++)
					if (g.In[v].empty()) starts.push_back(v);
				for (int v = 0; v < n; v++)
					if (!g.In[v].empty()) starts.push_back(v);

				std::vector<char> state(n);		// 0 not seen, 1 on the stack, 2 done
				std::vector<std::pair<int, size_t>> stack;
				for (int s : starts) {
					if (state[s]) continue;
					state[s] = 1;
					stack.push_back({ s, 0 });
					while (!stack.empty()) {
						int v = stack.back().first;
						size_t i = stack.back().second;
						if (i < g.Out[v].size()) {
							stack.back().second++;
							int e = g.Out[v][i], t = g.Edges[e].To;
							if (state[t] == 1)
								reversed[e] = 1;
							else if (state[t] == 0) {
								state[t] = 1;
								stack.push_back({ t, 0 });
							}
						}
						else {
							state[v] = 2;
							stack.pop_back();
						}
					}
				}
			}
			auto tail = [&](int e) { return reversed[e] ? g.Edges[e].To : g.Edges[e].From; };
			auto head = [&](int e) { return reversed[e] ? g.Edges[e].From : g.Edges[e].To; };

			// 2. Layers: the longest path from the sources, then the sources move down next to their first successor.
			std::vector<std::vector<int>> succ(n), pred(n);
			for (int e = 0; e < edgeCount; e++) {
				succ[tail(e)].push_back(head(e));
				pred[head(e)].push_back(tail(e));
			}
			std::vector<int> topo, inDegree(n);
			for (int v = 0; v < n; v++) {
				inDegree[v] = (int)pred[v].size();
				if (inDegree[v] == 0) topo.push_back(v);
			}
			for (size_t i = 0; i < topo.size(); i++)
				for (int t : succ[topo[i]])
					if (--inDegree[t] == 0) topo.push_back(t);

			std::vector<int> layer(n);
			for (int v : topo)
				for (int t : succ[v])
					layer[t] = std::max(layer[t], layer[v] + 1);
			for (auto it = topo.rbegin(); it != topo.rend(); ++it) {
				int v = *it;
				if (pred[v].empty() && !succ[v].empty()) {
					int lowest = INT_MAX;
					for (int t : succ[v]) lowest = std::min(lowest, layer[t]);
					layer[v] = lowest - 1;
				}
			}
			int layerCount = 0;
			for (int v = 0; v < n; v++)
				layerCount = std::max(layerCount, layer[v] + 1);

			// 3. An edge that crosses layers goes through a dummy node in each layer in between.
			struct Link { int To; float Weight; };
			std::vector<int> vLayer(layer);
			std::vector<float> vWidth(g.W);
			std::vector<std::vector<Link>> up(n), down(n);
			std::vector<std::vector<int>> chain(edgeCount);		// the dummies of each edge, from tail to head
			auto addVertex = [&](int l) {
				vLayer.push_back(l);
				vWidth.push_back(0.0f);
				up.emplace_back();
				down.emplace_back();
				return (int)vLayer.size() - 1;
			};
			auto connect = [&](int a, int b) {
				float w = (a >= n && b >= n) ? 8.0f : (a >= n || b >= n) ? 2.0f : 1.0f;	// keeps long edges straight
				down[a].push_back({ b, w });
				up[b].push_back({ a, w });
			};
			for (int e = 0; e < edgeCount; e++) {
				int prev = tail(e);
				for (int l = layer[tail(e)] + 1; l < layer[head(e)]; l++) {
					int d = addVertex(l);
					chain[e].push_back(d);
					connect(prev, d);
					prev = d;
				}
				connect(prev, head(e));
			}
			const int vertexCount = (int)vLayer.size();
			auto isDummy = [n](int v) { return v >= n; };

			// 4. The order in each layer. First the order of a DFS down the layers, then barycenter sweeps,
			// keeping the order with the fewest crossings.
			std::vector<std::vector<int>> layers(layerCount);
			{
				std::vector<int> seen(vertexCount, -1), stack;
				int counter = 0;
				std::vector<int> roots(n);
				std::iota(roots.begin(), roots.end(), 0);
				std::stable_sort(roots.begin(), roots.end(), [&](int a, int b) { return vLayer[a] < vLayer[b]; });
				for (int r : roots) {
					if (seen[r] >= 0) continue;
					stack.push_back(r);
					while (!stack.empty()) {
						int v = stack.back();
						stack.pop_back();
						if (seen[v] >= 0) continue;
						seen[v] = counter++;
						layers[vLayer[v]].push_back(v);
						for (auto it = down[v].rbegin(); it != down[v].rend(); ++it)
							if (seen[it->To] < 0) stack.push_back(it->To);
					}
				}
			}
			std::vector<int> pos(vertexCount);
			auto updatePositions = [&]() {
				for (const auto& lv : layers)
					for (int i = 0; i < (int)lv.size(); i++)
						pos[lv[i]] = i;
			};
			updatePositions();

			auto crossings = [&]() {
				long long total = 0;
				std::vector<std::pair<int, int>> pairs;
				std::vector<int> tree;		// Fenwick tree over the positions in the lower layer
				for (int l = 0; l + 1 < layerCount; l++) {
					pairs.clear();
					for (int v : layers[l])
						for (const auto& k : down[v])
							pairs.push_back({ pos[v], pos[k.To] });
					std::sort(pairs.begin(), pairs.end());
					int size = (int)layers[l + 1].size();
					tree.assign(size + 1, 0);
					int inserted = 0;
					for (const auto& p : pairs) {
						int notAfter = 0;
						for (int i = p.second + 1; i > 0; i -= i & -i) notAfter += tree[i];
						total += inserted - notAfter;
						for (int i = p.second + 1; i <= size; i += i & -i) tree[i]++;
						inserted++;
					}
				}
				return total;
			};

			auto best = layers;
			long long bestCrossings = crossings();
			std::vector<std::pair<float, int>> keyed;
			auto sortLayer = [&](int l, const std::vector<std::vector<Link>>& by) {
				keyed.clear();
				for (int v : layers[l]) {
					float sum = 0.0f;
					for (const auto& k : by[v]) sum += (float)pos[k.To];
					keyed.push_back({ by[v].empty() ? (float)pos[v] : sum / (float)by[v].size(), v });
				}
				std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
				for (int i = 0; i < (int)keyed.size(); i++) {
					layers[l][i] = keyed[i].second;
					pos[keyed[i].second] = i;
				}
			};
			for (int pass = 0; pass < o.CrossingPasses && bestCrossings > 0; pass++) {
				if (pass % 2 == 0)
					for (int l = 1; l < layerCount; l++) sortLayer(l, up);
				else
					for (int l = layerCount - 2; l >= 0; l--) sortLayer(l, down);
				long long c = crossings();
				if (c < bestCrossings) {
					bestCrossings = c;
					best = layers;
				}
			}
			layers = std::move(best);
			updatePositions();

			// 5. The positions along the layers: each node as close as it can be to the average of its neighbors,
			// sweeping down and up, without overlapping the nodes next to it.
			std::vector<float> x(vertexCount);
			auto gapBetween = [&](int a, int b) {
				return (vWidth[a] + vWidth[b]) * 0.5f + (isDummy(a) && isDummy(b) ? o.NodeSpacing * 0.5f : o.NodeSpacing);
			};
			for (const auto& lv : layers)
				for (size_t i = 1; i < lv.size(); i++)
					x[lv[i]] = x[lv[i - 1]] + gapBetween(lv[i - 1], lv[i]);

			std::vector<float> want, weight, gap, placed;
			auto placeLayer = [&](int l, bool useUp, bool useDown) {
				const auto& lv = layers[l];
				want.assign(lv.size(), 0.0f);
				weight.assign(lv.size(), 0.0f);
				gap.assign(lv.size(), 0.0f);
				for (size_t i = 0; i < lv.size(); i++) {
					int v = lv[i];
					float sum = 0.0f, sumW = 0.0f;
					if (useUp)
						for (const auto& k : up[v]) { sum += x[k.To] * k.Weight; sumW += k.Weight; }
					if (useDown)
						for (const auto& k : down[v]) { sum += x[k.To] * k.Weight; sumW += k.Weight; }
					want[i] = sumW > 0.0f ? sum / sumW : x[v];
					weight[i] = sumW > 0.0f ? sumW : 0.01f;
					if (i > 0) gap[i] = gapBetween(lv[i - 1], v);
				}
				PlaceInOrder(want, weight, gap, placed);
				for (size_t i = 0; i < lv.size(); i++)
					x[lv[i]] = placed[i];
			};
			for (int round = 0; round < 8; round++) {
				for (int l = 1; l < layerCount; l++) placeLayer(l, true, false);
				for (int l = layerCount - 2; l >= 0; l--) placeLayer(l, false, true);
			}
			for (int round = 0; round < 2; round++)
				for (int l = 0; l < layerCount; l++) placeLayer(l, true, true);

			// 6. The layers one under the other, each as deep as its tallest node.
			std::vector<float> layerY(layerCount), layerH(layerCount);
			for (int v = 0; v < n; v++)
				layerH[layer[v]] = std::max(layerH[layer[v]], g.H[v]);
			for (int l = 0; l < layerCount; l++)
				layerY[l] = l == 0 ? layerH[0] * 0.5f
					: layerY[l - 1] + (layerH[l - 1] + layerH[l]) * 0.5f + o.LayerSpacing;

			for (int v = 0; v < n; v++) {
				g.X[v] = x[v];
				g.Y[v] = layerY[layer[v]];
			}
			for (int e = 0; e < edgeCount; e++) {
				auto& bends = g.Bends[e];
				for (int d : chain[e])
					bends.push_back({ x[d], layerY[vLayer[d]] });
				if (reversed[e])
					std::reverse(bends.begin(), bends.end());
			}
		}

		// ---- Spanning tree (Tree, Radial) ------------------------------------------------------------------------

		struct SpanningTree {
			int Root;							// == the graph's Size() for a made up root over several roots
			std::vector<std::vector<int>> Children;
			std::vector<int> Order;				// breadth first, parents before children
			std::vector<int> Depth;
		};

		SpanningTree BuildTree(const Graph& g, int root) {
			const int n = g.Size();
			std::vector<int> roots;
			if (root >= 0)
				roots.push_back(root);
			else {
				for (int v = 0; v < n; v++)
					if (g.In[v].empty()) roots.push_back(v);
				if (roots.empty()) {	// all in cycles: the node with the most outgoing edges
					int top = 0;
					for (int v = 1; v < n; v++)
						if (g.Out[v].size() > g.Out[top].size()) top = v;
					roots.push_back(top);
				}
			}

			SpanningTree t;
			t.Root = roots.size() == 1 ? roots[0] : n;
			t.Children.assign(n + 1, {});
			t.Depth.assign(n + 1, 0);
			std::vector<char> seen(n + 1);
			seen[t.Root] = 1;
			t.Order.push_back(t.Root);
			if (t.Root == n)
				for (int r : roots) {
					seen[r] = 1;
					t.Children[n].push_back(r);
					t.Depth[r] = 1;
					t.Order.push_back(r);
				}

			// Along the edges first; whatever that can't reach hangs from a node it has an edge to.
			auto grow = [&](bool anyDirection) {
				for (size_t i = 0; i < t.Order.size(); i++) {
					int v = t.Order[i];
					if (v == n) continue;
					auto visit = [&](int u) {
						if (seen[u]) return;
						seen[u] = 1;
						t.Children[v].push_back(u);
						t.Depth[u] = t.Depth[v] + 1;
						t.Order.push_back(u);
					};
					for (int e : g.Out[v]) visit(g.Edges[e].To);
					if (anyDirection)
						for (int e : g.In[v]) visit(g.Edges[e].From);
				}
			};
			grow(false);
			if ((int)t.Order.size() < n + (t.Root == n ? 1 : 0)) {
				// the nodes reached so far are in breadth first order, so growing again from them keeps that order
				grow(true);
			}
			return t;
		}

		void Tree(Graph& g, const LayoutOptions& o, int root) {
			const int n = g.Size();
			SpanningTree t = BuildTree(g, root);
			const bool virtualRoot = t.Root == n;
			auto width = [&](int v) { return v == n ? 0.0f : g.W[v]; };

			// Bottom up: the outline of each subtree (left and right at each depth below its root), with the
			// subtrees of the children pushed as close as their outlines let them, the parent centered above.
			struct Outline { std::vector<float> L, R; };
			std::vector<Outline> outline(n + 1);
			std::vector<float> offset(n + 1);		// from the parent
			for (auto it = t.Order.rbegin(); it != t.Order.rend(); ++it) {
				int v = *it;
				const auto& kids = t.Children[v];
				Outline acc;
				if (!kids.empty()) {
					acc = std::move(outline[kids[0]]);
					offset[kids[0]] = 0.0f;
					for (size_t i = 1; i < kids.size(); i++) {
						Outline& c = outline[kids[i]];
						float shift = -FLT_MAX;
						size_t common = std::min(acc.L.size(), c.L.size());
						for (size_t k = 0; k < common; k++)
							shift = std::max(shift, acc.R[k] - c.L[k] + o.NodeSpacing);
						offset[kids[i]] = shift;
						for (size_t k = 0; k < c.L.size(); k++) {
							if (k < acc.L.size())
								acc.R[k] = c.R[k] + shift;
							else {
								acc.L.push_back(c.L[k] + shift);
								acc.R.push_back(c.R[k] + shift);
							}
						}
						c = {};
					}
					float mid = (offset[kids.front()] + offset[kids.back()]) * 0.5f;
					for (int k : kids) offset[k] -= mid;
					for (auto& l : acc.L) l -= mid;
					for (auto& r : acc.R) r -= mid;
				}
				Outline& mine = outline[v];
				mine.L.assign(1, -width(v) * 0.5f);
				mine.R.assign(1, width(v) * 0.5f);
				mine.L.insert(mine.L.end(), acc.L.begin(), acc.L.end());
				mine.R.insert(mine.R.end(), acc.R.begin(), acc.R.end());
			}

			// Top down: the positions, and each depth as deep as its tallest node.
			int maxDepth = 0;
			for (int v = 0; v < n; v++) maxDepth = std::max(maxDepth, t.Depth[v]);
			std::vector<float> levelH(maxDepth + 1), levelY(maxDepth + 1);
			for (int v = 0; v < n; v++)
				levelH[t.Depth[v]] = std::max(levelH[t.Depth[v]], g.H[v]);
			int first = virtualRoot ? 1 : 0;
			for (int d = first; d <= maxDepth; d++)
				levelY[d] = d == first ? levelH[d] * 0.5f : levelY[d - 1] + (levelH[d - 1] + levelH[d]) * 0.5f + o.LayerSpacing;

			std::vector<float> x(n + 1);
			for (int v : t.Order)
				for (int k : t.Children[v])
					x[k] = x[v] + offset[k];
			for (int v = 0; v < n; v++) {
				g.X[v] = x[v];
				g.Y[v] = levelY[t.Depth[v]];
			}
		}

		// The node from which the farthest node (ignoring the directions of the edges) is the closest.
		int CenterNode(const Graph& g) {
			const int n = g.Size();
			int best = 0, bestReach = INT_MAX;
			std::vector<int> distance(n), queue;
			for (int s = 0; s < n; s++) {
				std::fill(distance.begin(), distance.end(), -1);
				queue.assign(1, s);
				distance[s] = 0;
				int reach = 0;
				for (size_t i = 0; i < queue.size() && reach < bestReach; i++) {
					int v = queue[i];
					reach = distance[v];
					auto visit = [&](int u) { if (distance[u] < 0) { distance[u] = distance[v] + 1; queue.push_back(u); } };
					for (int e : g.Out[v]) visit(g.Edges[e].To);
					for (int e : g.In[v]) visit(g.Edges[e].From);
				}
				if (reach < bestReach) { bestReach = reach; best = s; }
			}
			return best;
		}

		void Radial(Graph& g, const LayoutOptions& o, int root) {
			const int n = g.Size();
			if (root < 0) {		// one root in the middle: the source that reaches the most, or the center if there is none
				int most = 0;
				std::vector<char> seen(n);
				std::vector<int> queue;
				for (int s = 0; s < n; s++) {
					if (!g.In[s].empty()) continue;
					std::fill(seen.begin(), seen.end(), char(0));
					queue.assign(1, s);
					seen[s] = 1;
					for (size_t i = 0; i < queue.size(); i++)
						for (int e : g.Out[queue[i]])
							if (!seen[g.Edges[e].To]) { seen[g.Edges[e].To] = 1; queue.push_back(g.Edges[e].To); }
					if ((int)queue.size() > most) { most = (int)queue.size(); root = s; }
				}
				if (root < 0) root = CenterNode(g);
			}
			SpanningTree t = BuildTree(g, root);

			// Each subtree gets a slice of the circle as large as its number of leaves.
			std::vector<float> leaves(n + 1);
			for (auto it = t.Order.rbegin(); it != t.Order.rend(); ++it) {
				int v = *it;
				if (t.Children[v].empty()) leaves[v] = 1.0f;
				for (int k : t.Children[v]) leaves[v] += leaves[k];
			}
			std::vector<float> angle(n + 1), from(n + 1), to(n + 1);
			from[t.Root] = -Pi * 0.5f;
			to[t.Root] = from[t.Root] + 2.0f * Pi;
			angle[t.Root] = from[t.Root];
			for (int v : t.Order) {
				float a = from[v], span = to[v] - from[v];
				for (int k : t.Children[v]) {
					from[k] = a;
					to[k] = a + span * leaves[k] / leaves[v];
					angle[k] = (from[k] + to[k]) * 0.5f;
					a = to[k];
				}
			}

			// The rings: far enough from the one inside and large enough that no two nodes on them meet.
			int maxDepth = 0;
			for (int v = 0; v < n; v++) maxDepth = std::max(maxDepth, t.Depth[v]);
			std::vector<std::vector<int>> rings(maxDepth + 1);
			for (int v = 0; v < n; v++) rings[t.Depth[v]].push_back(v);
			std::vector<float> radius(maxDepth + 1), size(maxDepth + 1);
			for (int d = 0; d <= maxDepth; d++)
				for (int v : rings[d]) size[d] = std::max(size[d], g.Extent(v));
			for (int d = 0; d <= maxDepth; d++) {
				auto& ring = rings[d];
				if (ring.empty()) continue;
				float r = d == 0 ? 0.0f : (d == 1 && rings[0].empty() ? 0.0f : radius[d - 1] + (size[d - 1] + size[d]) * 0.5f + o.LayerSpacing);
				if (ring.size() > 1) {
					std::sort(ring.begin(), ring.end(), [&](int a, int b) { return angle[a] < angle[b]; });
					for (size_t i = 0; i < ring.size(); i++) {
						int a = ring[i], b = ring[(i + 1) % ring.size()];
						float delta = angle[b] - angle[a];
						if (delta <= 0.0f) delta += 2.0f * Pi;
						float need = (g.Extent(a) + g.Extent(b)) * 0.5f + o.NodeSpacing;
						r = std::max(r, need / (2.0f * std::sin(std::min(delta, Pi) * 0.5f)));
					}
				}
				radius[d] = r;
			}
			for (int v = 0; v < n; v++) {
				g.X[v] = radius[t.Depth[v]] * std::cos(angle[v]);
				g.Y[v] = radius[t.Depth[v]] * std::sin(angle[v]);
			}
		}

		// ---- Circular --------------------------------------------------------------------------------------------

		void Circular(Graph& g, const LayoutOptions& o) {
			const int n = g.Size();
			if (n == 1) { g.X[0] = g.Y[0] = 0.0f; return; }

			// Depth first from the node with the most edges keeps neighbors next to each other.
			int start = 0;
			for (int v = 1; v < n; v++)
				if (g.Out[v].size() + g.In[v].size() > g.Out[start].size() + g.In[start].size()) start = v;
			std::vector<int> order, stack{ start };
			std::vector<char> seen(n);
			while (!stack.empty()) {
				int v = stack.back();
				stack.pop_back();
				if (seen[v]) continue;
				seen[v] = 1;
				order.push_back(v);
				for (auto it = g.In[v].rbegin(); it != g.In[v].rend(); ++it)
					if (!seen[g.Edges[*it].From]) stack.push_back(g.Edges[*it].From);
				for (auto it = g.Out[v].rbegin(); it != g.Out[v].rend(); ++it)
					if (!seen[g.Edges[*it].To]) stack.push_back(g.Edges[*it].To);
			}

			// Each node takes an arc as long as it is large; arcs are a bit longer than the straight line between
			// the centers, so the circle is made a bit larger.
			float circumference = 0.0f;
			for (int v : order) circumference += g.Extent(v) + o.NodeSpacing;
			float r = circumference / (2.0f * Pi) * 1.15f;
			float along = 0.0f;
			for (int v : order) {
				float half = (g.Extent(v) + o.NodeSpacing) * 0.5f;
				along += half;
				float a = along / circumference * 2.0f * Pi - Pi * 0.5f;
				g.X[v] = r * std::cos(a);
				g.Y[v] = r * std::sin(a);
				along += half;
			}
		}

		// ---- Force directed (Fruchterman-Reingold) -----------------------------------------------------------------

		void RemoveOverlaps(Graph& g, float spacing) {
			const int n = g.Size();
			for (int round = 0; round < 100; round++) {
				bool moved = false;
				for (int i = 0; i < n; i++)
					for (int j = i + 1; j < n; j++) {
						float dx = g.X[j] - g.X[i], dy = g.Y[j] - g.Y[i];
						float ox = (g.W[i] + g.W[j]) * 0.5f + spacing - std::fabs(dx);
						float oy = (g.H[i] + g.H[j]) * 0.5f + spacing - std::fabs(dy);
						if (ox <= 0.0f || oy <= 0.0f) continue;
						moved = true;
						if (ox < oy) {
							float s = (dx < 0.0f ? -ox : ox) * 0.5f;
							g.X[i] -= s; g.X[j] += s;
						}
						else {
							float s = (dy < 0.0f ? -oy : oy) * 0.5f;
							g.Y[i] -= s; g.Y[j] += s;
						}
					}
				if (!moved) break;
			}
		}

		void ForceDirected(Graph& g, const LayoutOptions& o) {
			const int n = g.Size();
			if (n == 1) { g.X[0] = g.Y[0] = 0.0f; return; }

			float average = 0.0f;
			for (int v = 0; v < n; v++) average += g.Extent(v);
			average /= n;
			const float k = average + o.NodeSpacing;	// the length the edges would like to have

			uint32_t state = o.Seed ? o.Seed : 1;
			auto random01 = [&]() {	// xorshift, [0, 1)
				state ^= state << 13; state ^= state >> 17; state ^= state << 5;
				return (state & 0xFFFFFF) / float(0x1000000);
			};
			const float side = k * std::sqrt((float)n) * 1.5f;
			for (int v = 0; v < n; v++) {
				g.X[v] = random01() * side;
				g.Y[v] = random01() * side;
			}

			const int iterations = std::max(o.Iterations, 1);
			std::vector<float> fx(n), fy(n);
			for (int it = 0; it < iterations; it++) {
				std::fill(fx.begin(), fx.end(), 0.0f);
				std::fill(fy.begin(), fy.end(), 0.0f);
				for (int i = 0; i < n; i++)
					for (int j = i + 1; j < n; j++) {
						float dx = g.X[i] - g.X[j], dy = g.Y[i] - g.Y[j];
						float d2 = dx * dx + dy * dy;
						if (d2 < 0.01f) {	// on top of each other: any direction
							dx = random01() - 0.5f; dy = random01() - 0.5f;
							d2 = dx * dx + dy * dy + 0.01f;
						}
						float f = k * k / d2;	// k^2 / d, along the unit vector (dx, dy) / d
						fx[i] += dx * f; fy[i] += dy * f;
						fx[j] -= dx * f; fy[j] -= dy * f;
					}
				for (const auto& e : g.Edges) {
					float dx = g.X[e.From] - g.X[e.To], dy = g.Y[e.From] - g.Y[e.To];
					float d = std::sqrt(dx * dx + dy * dy);
					float f = d / k;		// d^2 / k, along (dx, dy) / d
					fx[e.From] -= dx * f; fy[e.From] -= dy * f;
					fx[e.To] += dx * f;   fy[e.To] += dy * f;
				}
				float temperature = side * 0.1f * (1.0f - (float)it / iterations) + k * 0.01f;
				for (int v = 0; v < n; v++) {
					float len = std::sqrt(fx[v] * fx[v] + fy[v] * fy[v]);
					if (len < 1e-6f) continue;
					float step = std::min(len, temperature);
					g.X[v] += fx[v] / len * step;
					g.Y[v] += fy[v] / len * step;
				}
			}
			RemoveOverlaps(g, o.NodeSpacing * 0.5f);
		}

		// ---- Grid ------------------------------------------------------------------------------------------------

		void Grid(Graph& g, const LayoutOptions& o) {
			const int n = g.Size();
			int columns = o.GridColumns > 0 ? o.GridColumns : (int)std::ceil(std::sqrt((float)n));
			int rows = (n + columns - 1) / columns;
			std::vector<float> colW(columns), rowH(rows);
			for (int v = 0; v < n; v++) {
				colW[v % columns] = std::max(colW[v % columns], g.W[v]);
				rowH[v / columns] = std::max(rowH[v / columns], g.H[v]);
			}
			std::vector<float> colX(columns), rowY(rows);
			for (int c = 1; c < columns; c++)
				colX[c] = colX[c - 1] + (colW[c - 1] + colW[c]) * 0.5f + o.NodeSpacing;
			for (int r = 1; r < rows; r++)
				rowY[r] = rowY[r - 1] + (rowH[r - 1] + rowH[r]) * 0.5f + o.NodeSpacing;
			for (int v = 0; v < n; v++) {
				g.X[v] = colX[v % columns];
				g.Y[v] = rowY[v / columns];
			}
		}

		// ---- Connected parts -------------------------------------------------------------------------------------

		void LayoutPart(Graph& g, LayoutAlgorithm algorithm, const LayoutOptions& o, int root) {
			switch (algorithm) {
			case LayoutAlgorithm::Layered:       Layered(g, o); break;
			case LayoutAlgorithm::Tree:          Tree(g, o, root); break;
			case LayoutAlgorithm::Radial:        Radial(g, o, root); break;
			case LayoutAlgorithm::ForceDirected: ForceDirected(g, o); break;
			case LayoutAlgorithm::Circular:      Circular(g, o); break;
			case LayoutAlgorithm::Grid:          Grid(g, o); break;
			}
		}

		// Lays out each connected part by itself and puts them in rows, the largest first.
		void LayoutParts(Graph& g, LayoutAlgorithm algorithm, const LayoutOptions& o, int root) {
			const int n = g.Size();
			std::vector<int> part(n, -1);
			int partCount = 0;
			for (int s = 0; s < n; s++) {
				if (part[s] >= 0) continue;
				std::vector<int> stack{ s };
				part[s] = partCount;
				while (!stack.empty()) {
					int v = stack.back();
					stack.pop_back();
					auto visit = [&](int u) { if (part[u] < 0) { part[u] = partCount; stack.push_back(u); } };
					for (int e : g.Out[v]) visit(g.Edges[e].To);
					for (int e : g.In[v]) visit(g.Edges[e].From);
				}
				partCount++;
			}

			std::vector<Graph> parts(partCount);
			std::vector<std::vector<int>> nodesOf(partCount), edgesOf(partCount);
			std::vector<int> local(n);
			for (int v = 0; v < n; v++) {
				Graph& p = parts[part[v]];
				local[v] = p.Size();
				nodesOf[part[v]].push_back(v);
				p.W.push_back(g.W[v]); p.H.push_back(g.H[v]);
				p.X.push_back(g.X[v]); p.Y.push_back(g.Y[v]);
			}
			for (int e = 0; e < (int)g.Edges.size(); e++) {
				const LEdge& le = g.Edges[e];
				int pi = part[le.From];
				parts[pi].Edges.push_back({ local[le.From], local[le.To], le.Index });
				edgesOf[pi].push_back(e);
			}

			std::vector<Box> boxes(partCount);
			for (int pi = 0; pi < partCount; pi++) {
				Graph& p = parts[pi];
				p.Link();
				int partRoot = root >= 0 && part[root] == pi ? local[root] : -1;
				LayoutPart(p, algorithm, o, partRoot);
				boxes[pi] = Bounds(p);
			}

			// Shelf packing: rows about as wide as the whole would be if it were square.
			std::vector<int> byHeight(partCount);
			std::iota(byHeight.begin(), byHeight.end(), 0);
			std::stable_sort(byHeight.begin(), byHeight.end(), [&](int a, int b) { return boxes[a].Height() > boxes[b].Height(); });
			const float gap = o.NodeSpacing * 2.0f;
			float area = 0.0f, widest = 0.0f;
			for (const auto& b : boxes) {
				area += (b.Width() + gap) * (b.Height() + gap);
				widest = std::max(widest, b.Width());
			}
			float rowLimit = std::max(widest, std::sqrt(area) * 1.3f);
			float x = 0.0f, y = 0.0f, rowHeight = 0.0f;
			for (int pi : byHeight) {
				const Box& b = boxes[pi];
				if (x > 0.0f && x + b.Width() > rowLimit) {
					x = 0.0f;
					y += rowHeight + gap;
					rowHeight = 0.0f;
				}
				Translate(parts[pi], x - b.MinX, y - b.MinY);
				x += b.Width() + gap;
				rowHeight = std::max(rowHeight, b.Height());
			}

			for (int pi = 0; pi < partCount; pi++) {
				const Graph& p = parts[pi];
				for (int i = 0; i < p.Size(); i++) {
					g.X[nodesOf[pi][i]] = p.X[i];
					g.Y[nodesOf[pi][i]] = p.Y[i];
				}
				for (size_t i = 0; i < edgesOf[pi].size(); i++)
					g.Bends[edgesOf[pi][i]] = p.Bends[i];
			}
		}

	} // anonymous namespace

	void Layout(NodeGraphModel& model, LayoutAlgorithm algorithm, const LayoutOptions& options) {
		const auto& nodes = model.Nodes();
		const int n = (int)nodes.size();
		if (n == 0) return;

		std::unordered_map<NodeId, int> index;
		Graph g;
		Box before;
		for (int v = 0; v < n; v++) {
			const Node& node = nodes[v];
			index[node.Id] = v;
			g.W.push_back(node.Width); g.H.push_back(node.Height);
			g.X.push_back(node.X);     g.Y.push_back(node.Y);
			before.Add(node.X, node.Y, node.Width, node.Height);
		}
		const auto& edges = model.Edges();
		for (size_t i = 0; i < edges.size(); i++) {
			auto from = index.find(edges[i].From), to = index.find(edges[i].To);
			if (from != index.end() && to != index.end() && from->second != to->second)
				g.Edges.push_back({ from->second, to->second, i });
		}
		g.Link();

		// Layered and Tree work top to bottom; another direction turns the result, so they work on turned nodes.
		const bool directed = algorithm == LayoutAlgorithm::Layered || algorithm == LayoutAlgorithm::Tree;
		const LayoutDirection direction = directed ? options.Direction : LayoutDirection::TopToBottom;
		const bool across = direction == LayoutDirection::LeftToRight || direction == LayoutDirection::RightToLeft;
		if (across) std::swap(g.W, g.H);

		int root = -1;
		if (options.Root != InvalidNode)
			if (auto it = index.find(options.Root); it != index.end()) root = it->second;

		if (algorithm == LayoutAlgorithm::Grid)
			Grid(g, options);
		else
			LayoutParts(g, algorithm, options, root);

		auto turn = [direction](float& x, float& y) {
			switch (direction) {
			case LayoutDirection::TopToBottom: break;
			case LayoutDirection::BottomToTop: y = -y; break;
			case LayoutDirection::LeftToRight: std::swap(x, y); break;
			case LayoutDirection::RightToLeft: std::swap(x, y); x = -x; break;
			}
		};
		if (across) std::swap(g.W, g.H);
		Box after;
		for (int v = 0; v < n; v++) {
			turn(g.X[v], g.Y[v]);
			after.Add(g.X[v], g.Y[v], g.W[v], g.H[v]);
		}
		for (auto& bends : g.Bends)
			for (auto& p : bends) turn(p.X, p.Y);

		Translate(g, before.MinX - after.MinX, before.MinY - after.MinY);
		for (int v = 0; v < n; v++) {
			Node* node = model.GetNode(nodes[v].Id);
			node->X = g.X[v];
			node->Y = g.Y[v];
		}
		for (const auto& e : edges)
			model.GetEdge(e.Id)->Waypoints.clear();
		for (size_t e = 0; e < g.Edges.size(); e++)
			model.GetEdge(edges[g.Edges[e].Index].Id)->Waypoints = g.Bends[e];
	}

} // namespace NodeGraphCtrl
