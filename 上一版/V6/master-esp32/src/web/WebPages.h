#ifndef IRRIGATION_WEB_PAGES_H
#define IRRIGATION_WEB_PAGES_H

namespace Irrigation {
class ConfigStore;
class HistoryStore;
class Rs485Master;
class RuntimeController;

void registerWebPages(Rs485Master& master, RuntimeController& runtime, ConfigStore& config, HistoryStore& history);

}  // namespace Irrigation

#endif
