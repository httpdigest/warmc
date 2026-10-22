// Container adaptors and heaps: std::priority_queue as a max-heap, as a
// min-heap with std::greater, and over structs with a custom comparator
// (an event scheduler and a k-way merge); std::stack over deque, vector and
// list, used for bracket matching and an RPN evaluator; std::queue for a
// breadth-first search on a grid and a round-robin scheduler; and the heap
// algorithms (make_heap, push_heap, pop_heap, sort_heap, is_heap,
// is_heap_until) applied to a vector directly.
//
// Every pop is printed, so the output is the exact order the heap yields --
// including the order of equal priorities, which for a binary heap depends
// on the precise sift-up and sift-down sequence libc++ performs.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <list>
#include <queue>
#include <stack>
#include <string>
#include <vector>

namespace {

struct Event {
  int time;
  int seq;
  std::string name;
};
struct Later {
  bool operator()(const Event &a, const Event &b) const {
    return a.time != b.time ? a.time > b.time : a.seq > b.seq;
  }
};

bool balanced(const std::string &s, std::size_t &errAt) {
  std::stack<char, std::vector<char>> st;
  for (std::size_t i = 0; i < s.size(); i++) {
    char c = s[i];
    if (c == '(' || c == '[' || c == '{') {
      st.push(c);
    } else if (c == ')' || c == ']' || c == '}') {
      char want = c == ')' ? '(' : c == ']' ? '[' : '{';
      if (st.empty() || st.top() != want) {
        errAt = i;
        return false;
      }
      st.pop();
    }
  }
  errAt = s.size();
  return st.empty();
}

long rpn(const std::string &expr, bool &ok) {
  std::stack<long, std::list<long>> st;
  std::string tok;
  ok = true;
  auto flush = [&]() {
    if (tok.empty()) return;
    if (tok == "+" || tok == "-" || tok == "*" || tok == "/" || tok == "%") {
      if (st.size() < 2) {
        ok = false;
        tok.clear();
        return;
      }
      long b = st.top();
      st.pop();
      long a = st.top();
      st.pop();
      long r = 0;
      switch (tok[0]) {
        case '+': r = a + b; break;
        case '-': r = a - b; break;
        case '*': r = a * b; break;
        case '/': r = b ? a / b : (ok = false, 0); break;
        case '%': r = b ? a % b : (ok = false, 0); break;
      }
      st.push(r);
    } else {
      st.push(std::stol(tok));
    }
    tok.clear();
  };
  for (char c : expr) {
    if (c == ' ')
      flush();
    else
      tok += c;
  }
  flush();
  if (st.size() != 1) ok = false;
  return st.empty() ? 0 : st.top();
}

}  // namespace

int main() {
  std::printf("== priority_queue max and min\n");
  std::priority_queue<int> maxq;
  std::priority_queue<int, std::vector<int>, std::greater<int>> minq;
  uint32_t seed = 7;
  for (int i = 0; i < 25; i++) {
    seed = seed * 1664525u + 1013904223u;
    int v = (int)(seed >> 24) % 50;
    maxq.push(v);
    minq.push(v);
  }
  std::printf("max order:");
  while (!maxq.empty()) {
    std::printf(" %d", maxq.top());
    maxq.pop();
  }
  std::printf("\nmin order:");
  while (!minq.empty()) {
    std::printf(" %d", minq.top());
    minq.pop();
  }
  std::printf("\n");
  std::vector<int> init{5, 1, 8, 3, 9, 2};
  std::priority_queue<int> fromRange(init.begin(), init.end());
  fromRange.emplace(7);
  std::printf("from range top=%d size=%zu\n", fromRange.top(), fromRange.size());

  std::printf("== event scheduler\n");
  std::priority_queue<Event, std::vector<Event>, Later> events;
  int seq = 0;
  events.push({10, seq++, "boot"});
  events.push({5, seq++, "init"});
  events.push({10, seq++, "mount"});
  events.push({1, seq++, "power"});
  events.push({20, seq++, "login"});
  int now = 0;
  while (!events.empty()) {
    Event e = events.top();
    events.pop();
    now = e.time;
    std::printf("t=%2d %s (seq %d)\n", now, e.name.c_str(), e.seq);
    if (e.name == "boot") events.push({now + 3, seq++, "boot-done"});
    if (e.name == "mount") events.push({now, seq++, "mount-ack"});
    if (e.name == "login" && now < 40) events.push({now + 7, seq++, "login"});
  }

  std::printf("== k-way merge\n");
  std::vector<std::vector<int>> lists = {
      {1, 4, 9, 16, 25}, {2, 3, 5, 7, 11, 13}, {0, 10, 20, 30}, {}, {6, 6, 6}};
  using Item = std::pair<int, std::pair<int, int>>;  // value, (list, index)
  std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heads;
  for (int i = 0; i < (int)lists.size(); i++)
    if (!lists[i].empty()) heads.push({lists[i][0], {i, 0}});
  std::printf("merged:");
  while (!heads.empty()) {
    auto [v, where] = heads.top();
    heads.pop();
    std::printf(" %d", v);
    auto [li, idx] = where;
    if (idx + 1 < (int)lists[li].size()) heads.push({lists[li][idx + 1], {li, idx + 1}});
  }
  std::printf("\n");

  std::printf("== stacks\n");
  for (const char *s : {"([]{()})", "([)]", "((()", "{[()()]}[]", "", "]", "a(b[c]{d}e)f"}) {
    std::size_t err;
    bool ok = balanced(s, err);
    std::printf("\"%s\" balanced=%d stop=%zu\n", s, ok, err);
  }
  for (const char *e : {"3 4 +", "5 1 2 + 4 * + 3 -", "2 3 4 * 5 + -", "10 3 %", "1 +",
                        "4 0 /", "7 2 - 3 * 100 9 % +"}) {
    bool ok;
    long r = rpn(e, ok);
    std::printf("rpn \"%s\" = %ld ok=%d\n", e, r, ok);
  }
  std::stack<int> sd;
  for (int i = 0; i < 5; i++) sd.push(i * i);
  std::stack<int> sd2 = sd;
  sd2.pop();
  std::printf("stack sizes %zu %zu tops %d %d equal=%d less=%d\n", sd.size(), sd2.size(),
              sd.top(), sd2.top(), sd == sd2, sd2 < sd);

  std::printf("== BFS with queue\n");
  const char *maze[] = {"S.#.......",
                        ".##.####.#",
                        "....#..#..",
                        "#.#...#..#",
                        "..#.#.##.E"};
  const int R = 5, C = 10;
  std::vector<std::vector<int>> dist(R, std::vector<int>(C, -1));
  std::queue<std::pair<int, int>> q;
  q.push({0, 0});
  dist[0][0] = 0;
  int visited = 0;
  while (!q.empty()) {
    auto [r, c] = q.front();
    q.pop();
    ++visited;
    const int dr[] = {1, -1, 0, 0}, dc[] = {0, 0, 1, -1};
    for (int k = 0; k < 4; k++) {
      int nr = r + dr[k], nc = c + dc[k];
      if (nr < 0 || nr >= R || nc < 0 || nc >= C) continue;
      if (maze[nr][nc] == '#' || dist[nr][nc] >= 0) continue;
      dist[nr][nc] = dist[r][c] + 1;
      q.push({nr, nc});
    }
  }
  for (int r = 0; r < R; r++) {
    for (int c = 0; c < C; c++) {
      if (maze[r][c] == '#')
        std::printf("  #");
      else
        std::printf("%3d", dist[r][c]);
    }
    std::printf("\n");
  }
  std::printf("visited=%d distance to E=%d\n", visited, dist[4][9]);

  std::printf("== round robin\n");
  std::queue<std::pair<std::string, int>> jobs;
  jobs.push({"A", 5});
  jobs.push({"B", 2});
  jobs.push({"C", 7});
  jobs.push({"D", 3});
  const int quantum = 2;
  int clock = 0;
  std::string timeline;
  while (!jobs.empty()) {
    auto [name, left] = jobs.front();
    jobs.pop();
    int run = std::min(quantum, left);
    clock += run;
    timeline += name + std::to_string(run) + " ";
    if (left > run)
      jobs.push({name, left - run});
    else
      std::printf("%s finishes at %d\n", name.c_str(), clock);
  }
  std::printf("timeline: %s\n", timeline.c_str());

  std::printf("== heap algorithms\n");
  std::vector<int> h{3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9};
  std::printf("is_heap before=%d until=%ld\n", std::is_heap(h.begin(), h.end()),
              (long)(std::is_heap_until(h.begin(), h.end()) - h.begin()));
  std::make_heap(h.begin(), h.end());
  std::printf("heap:");
  for (int x : h) std::printf(" %d", x);
  std::printf("\nis_heap after=%d\n", std::is_heap(h.begin(), h.end()));
  h.push_back(10);
  std::push_heap(h.begin(), h.end());
  std::printf("after push 10 front=%d\n", h.front());
  for (int i = 0; i < 4; i++) {
    std::pop_heap(h.begin(), h.end());
    std::printf("popped %d\n", h.back());
    h.pop_back();
  }
  std::sort_heap(h.begin(), h.end());
  std::printf("sort_heap:");
  for (int x : h) std::printf(" %d", x);
  std::printf("\n");
  std::vector<int> mh{9, 4, 7, 1, 8};
  std::make_heap(mh.begin(), mh.end(), std::greater<>());
  std::printf("min-heap front=%d:", mh.front());
  for (int x : mh) std::printf(" %d", x);
  std::printf("\n");
  return 0;
}
