#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "sim_core/script_vm.hpp"
#include "sim_core/sim_state.hpp"

namespace {

struct Config {
  std::uint64_t seed = 0;
  std::uint64_t ticks = 60;
  std::uint64_t hash_every = 0;
  std::string script;
};

bool LoadJsonConfig(const std::string& path, Config& config) {
  std::ifstream file(path);
  if (!file) {
    return false;
  }

  nlohmann::json json;
  file >> json;

  if (json.contains("seed")) {
    config.seed = json.at("seed").get<std::uint64_t>();
  }
  if (json.contains("ticks")) {
    config.ticks = json.at("ticks").get<std::uint64_t>();
  }
  if (json.contains("hash_every")) {
    config.hash_every = json.at("hash_every").get<std::uint64_t>();
  }
  if (json.contains("script")) {
    config.script = json.at("script").get<std::string>();
  }

  return true;
}

void PrintUsage() {
  std::cout << "sim_cli options:\n"
            << "  --config <path>     Optional JSON config\n"
            << "  --seed <u64>        RNG seed\n"
            << "  --ticks <u64>       Number of ticks to run\n"
            << "  --hash-every <u64>  Print hash every N ticks\n"
            << "  --script <path>     Lua script file\n";
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  std::string config_path;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      config_path = argv[++i];
    } else if (arg == "--seed" && i + 1 < argc) {
      config.seed = std::stoull(argv[++i]);
    } else if (arg == "--ticks" && i + 1 < argc) {
      config.ticks = std::stoull(argv[++i]);
    } else if (arg == "--hash-every" && i + 1 < argc) {
      config.hash_every = std::stoull(argv[++i]);
    } else if (arg == "--script" && i + 1 < argc) {
      config.script = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage();
      return 0;
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      PrintUsage();
      return 1;
    }
  }

  if (!config_path.empty()) {
    if (!LoadJsonConfig(config_path, config)) {
      std::cerr << "Failed to load config: " << config_path << "\n";
      return 1;
    }
  }

  arksim::SimState sim(config.seed);
  arksim::ScriptVM script_vm;

  if (!config.script.empty()) {
    if (!script_vm.load_file(config.script)) {
      std::cerr << "Script error: " << script_vm.last_error() << "\n";
      return 1;
    }
    sim.attach_script(&script_vm);
  }

  for (std::uint64_t i = 0; i < config.ticks; ++i) {
    sim.step();
    if (config.hash_every > 0 && (i + 1) % config.hash_every == 0) {
      std::cout << "tick=" << sim.tick() << " hash=" << sim.state_hash() << "\n";
    }
  }

  if (config.hash_every == 0) {
    std::cout << "tick=" << sim.tick() << " hash=" << sim.state_hash() << "\n";
  }

  return 0;
}
