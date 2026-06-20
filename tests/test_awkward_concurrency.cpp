#include <awkward/awkward.hpp>

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

int main() {
    const auto base = ak::with_named_axis(
        ak::with_attrs(ak::from_iter<int>({{1, ak::none, 3}, {}, {4, 5}}), {{"source", "concurrency"}}),
        "rows", 0);
    const auto expected = ak::to_list(base);
    std::atomic<bool> passed{true};

    std::vector<std::thread> readers;
    for (std::size_t thread = 0; thread < 8; ++thread) {
        readers.emplace_back([&base, &expected, &passed] {
            for (std::size_t iteration = 0; iteration < 500; ++iteration) {
                if (ak::to_list(base) != expected || !base.is_valid() || base.ndim() != 2 ||
                    base.attrs().at("source") != "concurrency" ||
                    ak::to_list(ak::fill_none(base, 0)) !=
                        ak::Value(ak::Value::list_type{
                            ak::Value::list_type{1, 0, 3}, ak::Value::list_type{}, ak::Value::list_type{4, 5}})) {
                    passed.store(false, std::memory_order_relaxed);
                    return;
                }
                const auto buffers = ak::to_buffers(base);
                if (ak::to_list(ak::from_buffers(buffers)) != expected) {
                    passed.store(false, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }
    for (auto& reader : readers) reader.join();
    return passed.load(std::memory_order_relaxed) ? 0 : 1;
}
