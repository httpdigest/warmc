// A C++ host of libwarmc: compiles example/jit/guest.wat, gives it a host
// function written as a lambda, calls its exports and reads its memory,
// through the standard WebAssembly C API (wasm.h), each object owned by a
// std::unique_ptr that deletes it.
//
//   clang++ -std=c++17 -Iinclude -o host example/jit/cpp/host.cpp -Lbuild -lwarmc -Wl,-rpath,build
//   ./host example/jit/guest.wat
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "wasm.h"
#include "warm.h"

namespace {

template <typename T, void (*Delete)(T *)>
struct Deleter {
  void operator()(T *p) const { Delete(p); }
};
template <typename T, void (*Delete)(T *)>
using Own = std::unique_ptr<T, Deleter<T, Delete>>;

using Engine = Own<wasm_engine_t, wasm_engine_delete>;
using Store = Own<wasm_store_t, wasm_store_delete>;
using Module = Own<wasm_module_t, wasm_module_delete>;
using Instance = Own<wasm_instance_t, wasm_instance_delete>;
using Func = Own<wasm_func_t, wasm_func_delete>;
using Trap = Own<wasm_trap_t, wasm_trap_delete>;

// A wasm_byte_vec_t that deletes itself
struct Bytes {
  wasm_byte_vec_t v = WASM_EMPTY_VEC;
  Bytes() = default;
  Bytes(const Bytes &) = delete;
  ~Bytes() { wasm_byte_vec_delete(&v); }
  std::string str() const { return v.size ? std::string(v.data, v.size - (v.data[v.size - 1] == 0)) : ""; }
};

// A host function: what wasm calls it with, and what it gives back
using HostFn = std::function<void(const wasm_val_vec_t *args, wasm_val_vec_t *results)>;

wasm_trap_t *call_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
  (*static_cast<HostFn *>(env))(args, results);
  return nullptr;
}

// A function of the given parameter kinds and none back, calling fn
Func host_func(wasm_store_t *store, std::vector<wasm_valkind_t> params, HostFn *fn) {
  wasm_valtype_vec_t ps, rs;
  wasm_valtype_vec_new_uninitialized(&ps, params.size());
  for (size_t i = 0; i < params.size(); i++) ps.data[i] = wasm_valtype_new(params[i]);
  wasm_valtype_vec_new_empty(&rs);
  wasm_functype_t *type = wasm_functype_new(&ps, &rs);
  Func f(wasm_func_new_with_env(store, type, call_host, fn, nullptr));
  wasm_functype_delete(type);
  return f;
}

// The instance's exports by name
struct Exports {
  wasm_extern_vec_t v = WASM_EMPTY_VEC;
  std::vector<std::string> names;
  Exports(const wasm_module_t *module, const wasm_instance_t *instance) {
    wasm_instance_exports(instance, &v);
    wasm_exporttype_vec_t types;
    wasm_module_exports(module, &types);
    for (size_t i = 0; i < types.size; i++) {
      const wasm_name_t *n = wasm_exporttype_name(types.data[i]);
      names.emplace_back(n->data, n->size);
    }
    wasm_exporttype_vec_delete(&types);
  }
  ~Exports() { wasm_extern_vec_delete(&v); }
  wasm_extern_t *operator[](const std::string &name) const {
    for (size_t i = 0; i < names.size(); i++)
      if (names[i] == name) return v.data[i];
    throw std::runtime_error("no export " + name);
  }
};

// Calls fn with args, results going to results: the trap's message, or "".
std::string call(wasm_func_t *fn, std::vector<wasm_val_t> args, std::vector<wasm_val_t> &results) {
  wasm_val_vec_t av = { args.size(), args.data() }, rv = { results.size(), results.data() };
  Trap trap(wasm_func_call(fn, &av, &rv));
  if (!trap) return "";
  Bytes msg;
  wasm_trap_message(trap.get(), &msg.v);
  return msg.str();
}

}  // namespace

int main(int argc, char **argv) {
  std::ifstream in(argc > 1 ? argv[1] : "example/jit/guest.wat", std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  std::string wat = text.str();

  Engine engine(wasm_engine_new());
  Store store(wasm_store_new(engine.get()));
  Bytes binary, error;
  if (!warm_wat2wasm(wat.data(), wat.size(), &binary.v, &error.v)) {
    std::fprintf(stderr, "%s\n", error.str().c_str());
    return 1;
  }
  Module module(warm_module_new(store.get(), &binary.v, &error.v));
  if (!module) {
    std::fprintf(stderr, "%s\n", error.str().c_str());
    return 1;
  }

  // host.log(ptr, len), reading the guest's memory once there is one
  wasm_memory_t *memory = nullptr;
  HostFn log = [&memory](const wasm_val_vec_t *args, wasm_val_vec_t *) {
    auto ptr = static_cast<uint32_t>(args->data[0].of.i32);
    auto len = static_cast<uint32_t>(args->data[1].of.i32);
    std::printf("log: %.*s\n", static_cast<int>(len), wasm_memory_data(memory) + ptr);
  };
  Func log_func = host_func(store.get(), { WASM_I32, WASM_I32 }, &log);

  wasm_extern_t *imports[1] = { wasm_func_as_extern(log_func.get()) };
  wasm_extern_vec_t import_vec = WASM_ARRAY_VEC(imports);
  Instance instance(wasm_instance_new(store.get(), module.get(), &import_vec, nullptr));
  if (!instance) {
    std::fprintf(stderr, "cannot instantiate\n");
    return 1;
  }
  Exports exports(module.get(), instance.get());
  memory = wasm_extern_as_memory(exports["memory"]);

  std::vector<wasm_val_t> none;
  call(wasm_extern_as_func(exports["greet"]), {}, none);

  std::vector<wasm_val_t> r(1);
  call(wasm_extern_as_func(exports["fib"]), { WASM_I64_VAL(90) }, r);
  std::printf("fib(90) = %lld\n", static_cast<long long>(r[0].of.i64));

  for (auto [a, b] : { std::pair<int32_t, int32_t>{ 7, 2 }, { 1, 0 } }) {
    std::string trap = call(wasm_extern_as_func(exports["div"]), { WASM_I32_VAL(a), WASM_I32_VAL(b) }, r);
    if (trap.empty()) std::printf("div(%d, %d) = %d\n", a, b, r[0].of.i32);
    else std::printf("div(%d, %d) trapped: %s\n", a, b, trap.c_str());
  }
  for (int32_t addr : { 16, 65536 }) {
    std::string trap = call(wasm_extern_as_func(exports["peek"]), { WASM_I32_VAL(addr) }, r);
    if (trap.empty()) std::printf("peek(%d) = %d\n", addr, r[0].of.i32);
    else std::printf("peek(%d) trapped: %s\n", addr, trap.c_str());
  }
  return 0;
}
