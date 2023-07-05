#include "modules/script.hpp"

#include "drawtypes/label.hpp"
#include "modules/meta/base.inl"

POLYBAR_NS

namespace modules {
  script_module::script_module(const bar_settings& bar, string name_, const config& config)
      : module<script_module>(bar, move(name_), config)
      , m_tail(m_conf[config::value::MODULES_ENTRY][name_raw()]["tail"].as<bool>(false))
      , m_interval_success(m_conf[config::value::MODULES_ENTRY][name_raw()]["interval"].as<script_runner::interval>(m_tail ? 0s : 5s))
      , m_interval_fail(m_conf[config::value::MODULES_ENTRY][name_raw()]["interval-fail"].as<script_runner::interval>(m_interval_success))
      , m_interval_if(m_conf[config::value::MODULES_ENTRY][name_raw()]["interval-if"].as<script_runner::interval>(m_interval_success))
      , m_runner([this](const auto& data) { handle_runner_update(data); }, m_conf[config::value::MODULES_ENTRY][name_raw()]["exec"].as<string>(""s),
            m_conf[config::value::MODULES_ENTRY][name_raw()]["exec-if"].as<string>(""s), m_tail, m_interval_success, m_interval_fail,
            m_conf.get_with_prefix(name(), "env-")) {
    config::value module_config = m_conf[config::value::MODULES_ENTRY][name_raw()];

    // Load configured click handlers
    m_actions[mousebtn::LEFT] = module_config["click-left"].as<string>(""s);
    m_actions[mousebtn::MIDDLE] = module_config["click-middle"].as<string>(""s);
    m_actions[mousebtn::RIGHT] = module_config["click-right"].as<string>(""s);
    m_actions[mousebtn::DOUBLE_LEFT] = module_config["double-click-left"].as<string>(""s);
    m_actions[mousebtn::DOUBLE_MIDDLE] = module_config["double-click-middle"].as<string>(""s);
    m_actions[mousebtn::DOUBLE_RIGHT] = module_config["double-click-right"].as<string>(""s);
    m_actions[mousebtn::SCROLL_UP] = module_config["scroll-up"].as<string>(""s);
    m_actions[mousebtn::SCROLL_DOWN] = module_config["scroll-down"].as<string>(""s);

    // Setup formatting
    m_formatter->add(DEFAULT_FORMAT, TAG_LABEL, {TAG_LABEL});
    if (m_formatter->has(TAG_LABEL)) {
      m_label = load_optional_label(module_config[NAME_LABEL], "%output%");
    }

    m_formatter->add_optional(FORMAT_FAIL, {TAG_LABEL_FAIL});
    if (m_formatter->has(TAG_LABEL_FAIL)) {
      m_label_fail = load_optional_label(module_config[NAME_LABEL_FAIL], "%output%");
    }
  }

  /**
   * Start the module worker
   */
  void script_module::start() {
    this->module::start();
    m_mainthread = thread([&] {
      try {
        while (running()) {
          script_runner::interval sleep_time;
          if (m_runner.check_condition()) {
            sleep_time = m_runner.process();
          } else {
            m_runner.clear_output();
            sleep_time = std::max(m_interval_if, script_runner::interval(1s));
          }

          if (m_runner.is_stopping()) {
            break;
          }

          sleep(sleep_time);
        }
      } catch (const exception& err) {
        halt(err.what());
      }
    });
  }

  /**
   * Stop the module worker by terminating any running commands
   */
  void script_module::stop() {
    m_runner.stop();
    wakeup();

    module::stop();
  }

  /**
   * Generate module output
   */
  string script_module::get_format() const {
    if (m_exit_status != 0 && m_conf[config::value::MODULES_ENTRY][name_raw()].has(FORMAT_FAIL)) {
      return FORMAT_FAIL;
    }
    return DEFAULT_FORMAT;
  }

  string script_module::get_output() {
    auto data = [this] {
      std::lock_guard<std::mutex> lk(m_data_mutex);
      return m_data;
    }();

    m_exit_status = data.exit_status;

    if (data.output.empty() && m_exit_status == 0) {
      return "";
    }

    if (m_label) {
      m_label->reset_tokens();
      m_label->replace_token("%output%", data.output);
    }

    if (m_label_fail) {
      m_label_fail->reset_tokens();
      m_label_fail->replace_token("%output%", data.output);
    }

    string cnt{to_string(data.counter)};
    string output{module::get_output()};

    for (const auto& a : m_actions) {
      auto btn = a.first;
      auto action = a.second;

      if (!action.empty()) {
        auto action_replaced = string_util::replace_all(action, "%counter%", cnt);

        /*
         * The pid token is only for tailed commands.
         * If the command is not specified or running, replacement is unnecessary as well
         */
        if (data.pid != -1) {
          action_replaced = string_util::replace_all(action_replaced, "%pid%", to_string(data.pid));
        }
        m_builder->action(btn, action_replaced);
      }
    }

    m_builder->node(output);

    return m_builder->flush();
  }

  /**
   * Output format tags
   */
  bool script_module::build(builder* builder, const string& tag) const {
    if (tag == TAG_LABEL) {
      builder->node(m_label);
    } else if (tag == TAG_LABEL_FAIL) {
      builder->node(m_label_fail);
    } else {
      return false;
    }

    return true;
  }

  void script_module::handle_runner_update(const script_runner::data& data) {
    {
      std::lock_guard<std::mutex> lk(m_data_mutex);
      m_data = data;
    }

    broadcast();
  }
} // namespace modules

POLYBAR_NS_END
