// Command entrypoint for the native Rust terrain benchmark.

fn main() -> Result<(), Box<dyn std::error::Error>> {
    ofg_test_harness::terrain_bench::run()
}
