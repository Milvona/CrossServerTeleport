#include "mod/CrossServerTeleport.h"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/Overload.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/Listener.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/mod/RegisterHelper.h"

#include "mc/network/packet/TransferPacket.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

namespace cross_server_teleport {
namespace {

constexpr auto PluginPrefix = "§6[跨服传送] §r";

constexpr auto IconServer   = "textures/ui/servers";
constexpr auto IconSettings = "textures/ui/settings_glyph_color_2x";
constexpr auto IconList     = "textures/ui/world_glyph_color_2x";
constexpr auto IconAdd      = "textures/ui/color_plus";
constexpr auto IconEdit     = "textures/ui/icon_setting";
constexpr auto IconDelete   = "textures/ui/trash_default";
constexpr auto IconReload   = "textures/ui/refresh";
constexpr auto IconBack     = "textures/ui/arrow_left";
constexpr auto IconCancel   = "textures/ui/cancel";
constexpr auto IconConfirm  = "textures/ui/check";

std::string replaceAll(std::string text, std::string const& from, std::string const& to) {
    if (from.empty()) return text;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::string trim(std::string value) {
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::pair<std::string, unsigned short> parseAddress(std::string const& value) {
    auto address = trim(value);
    auto port    = static_cast<unsigned short>(19132);
    auto colon   = address.rfind(':');
    if (colon != std::string::npos) {
        auto portText = address.substr(colon + 1);
        address       = address.substr(0, colon);
        try {
            auto parsed = std::stoi(portText);
            if (parsed > 0 && parsed <= 65535) port = static_cast<unsigned short>(parsed);
        } catch (...) {
            port = 19132;
        }
    }
    return {trim(address), port};
}

template <class T>
T getValue(ll::form::CustomFormResult const& data, std::string const& name, T fallback) {
    if (!data) return fallback;
    auto it = data->find(name);
    if (it == data->end()) return fallback;

    if constexpr (std::is_same_v<T, std::string>) {
        if (auto value = std::get_if<std::string>(&it->second)) return *value;
    } else if constexpr (std::is_same_v<T, bool>) {
        if (auto value = std::get_if<uint64>(&it->second)) return *value != 0;
    } else if constexpr (std::is_integral_v<T>) {
        if (auto value = std::get_if<uint64>(&it->second)) return static_cast<T>(*value);
        if (auto value = std::get_if<double>(&it->second)) return static_cast<T>(*value);
    } else if constexpr (std::is_floating_point_v<T>) {
        if (auto value = std::get_if<double>(&it->second)) return static_cast<T>(*value);
        if (auto value = std::get_if<uint64>(&it->second)) return static_cast<T>(*value);
    }
    return fallback;
}

nlohmann::ordered_json toJson(CrossServerTeleport::SoundConfig const& value) {
    return {
        {"enabled", value.enabled},
        {"soundId", value.soundId},
        {"volume", value.volume},
        {"pitch", value.pitch},
    };
}

nlohmann::ordered_json toJson(CrossServerTeleport::ParticleConfig const& value) {
    return {
        {"enabled", value.enabled},
        {"particleId", value.particleId},
        {"count", value.count},
    };
}

nlohmann::ordered_json toJson(CrossServerTeleport::TitleConfig const& value) {
    return {
        {"enabled", value.enabled},
        {"mainTitle", value.mainTitle},
        {"subtitle", value.subtitle},
        {"fadeIn", value.fadeIn},
        {"stay", value.stay},
        {"fadeOut", value.fadeOut},
    };
}

nlohmann::ordered_json toJson(CrossServerTeleport::ServerConfig const& value) {
    return {
        {"id", value.id},
        {"name", value.name},
        {"address", value.address},
        {"description", value.description},
        {"icon", value.icon},
        {"enabled", value.enabled},
        {"requirePermission", value.requirePermission},
        {"permissionNode", value.permissionNode},
    };
}

void fromJson(nlohmann::json const& data, CrossServerTeleport::SoundConfig& value) {
    value.enabled = data.value("enabled", value.enabled);
    value.soundId = data.value("soundId", value.soundId);
    value.volume  = data.value("volume", value.volume);
    value.pitch   = data.value("pitch", value.pitch);
}

void fromJson(nlohmann::json const& data, CrossServerTeleport::ParticleConfig& value) {
    value.enabled    = data.value("enabled", value.enabled);
    value.particleId = data.value("particleId", value.particleId);
    value.count      = data.value("count", value.count);
}

void fromJson(nlohmann::json const& data, CrossServerTeleport::TitleConfig& value) {
    value.enabled   = data.value("enabled", value.enabled);
    value.mainTitle = data.value("mainTitle", value.mainTitle);
    value.subtitle  = data.value("subtitle", value.subtitle);
    value.fadeIn    = data.value("fadeIn", value.fadeIn);
    value.stay      = data.value("stay", value.stay);
    value.fadeOut   = data.value("fadeOut", value.fadeOut);
}

void fromJson(nlohmann::json const& data, CrossServerTeleport::ServerConfig& value) {
    value.id                = data.value("id", value.id);
    value.name              = data.value("name", value.name);
    value.address           = data.value("address", value.address);
    value.description       = data.value("description", value.description);
    value.icon              = data.value("icon", value.icon);
    value.enabled           = data.value("enabled", value.enabled);
    value.requirePermission = data.value("requirePermission", value.requirePermission);
    value.permissionNode    = data.value("permissionNode", value.permissionNode);
}

} // namespace

CrossServerTeleport& CrossServerTeleport::getInstance() {
    static CrossServerTeleport instance;
    return instance;
}

bool CrossServerTeleport::load() {
    getSelf().getLogger().debug("Loading cross-server teleport...");
    mConfigPath = getSelf().getConfigDir() / "config.json";
    loadConfig();
    return true;
}

bool CrossServerTeleport::enable() {
    getSelf().getLogger().debug("Enabling cross-server teleport...");
    registerCommands();
    registerTickListener();
    getSelf().getLogger().info("CrossServerTeleport loaded. Commands: /servertp, /stp");
    return true;
}

bool CrossServerTeleport::disable() {
    auto& bus = ll::event::EventBus::getInstance();
    if (mTickListener) {
        bus.removeListener(mTickListener);
        mTickListener.reset();
    }
    mPendingTeleports.clear();
    getSelf().getLogger().debug("Cross-server teleport disabled.");
    return true;
}

void CrossServerTeleport::ensureDefaultServers() {
    if (!mConfig.servers.empty()) return;

    mConfig.servers.push_back({
        "survival",
        "§a生存服务器",
        "localhost:19132",
        "§7主服务器 - 生存模式\n§e在线玩家: {online}",
        IconList,
        true,
        false,
        "crossserverteleport.survival",
    });
    mConfig.servers.push_back({
        "creative",
        "§b创造服务器",
        "localhost:19133",
        "§7创造服务器 - 建筑模式\n§e在线玩家: {online}",
        "textures/ui/creative_icon",
        true,
        false,
        "crossserverteleport.creative",
    });
}

void CrossServerTeleport::loadConfig() {
    ensureDefaultServers();

    try {
        std::filesystem::create_directories(mConfigPath.parent_path());
        if (!std::filesystem::exists(mConfigPath)) {
            saveConfig();
            return;
        }

        std::ifstream file(mConfigPath);
        auto data = nlohmann::json::parse(file, nullptr, true, true);

        mConfig.enabled           = data.value("enabled", mConfig.enabled);
        mConfig.teleportDelay     = data.value("teleportDelay", mConfig.teleportDelay);
        mConfig.requirePermission = data.value("requirePermission", mConfig.requirePermission);
        mConfig.permissionNode    = data.value("permissionNode", mConfig.permissionNode);

        if (data.contains("effects")) {
            auto const& effects = data["effects"];
            if (effects.contains("sound")) fromJson(effects["sound"], mConfig.effects.sound);
            if (effects.contains("particle")) fromJson(effects["particle"], mConfig.effects.particle);
            if (effects.contains("title")) fromJson(effects["title"], mConfig.effects.title);
        }

        if (data.contains("servers") && data["servers"].is_array()) {
            mConfig.servers.clear();
            for (auto const& item : data["servers"]) {
                ServerConfig server;
                fromJson(item, server);
                if (server.permissionNode.empty() && !server.id.empty()) {
                    server.permissionNode = "crossserverteleport." + server.id;
                }
                mConfig.servers.push_back(std::move(server));
            }
        }

        ensureDefaultServers();
        saveConfig();
    } catch (std::exception const& error) {
        getSelf().getLogger().error("Failed to load config: {}", error.what());
        mConfig = {};
        ensureDefaultServers();
        saveConfig();
    }
}

bool CrossServerTeleport::saveConfig() const {
    try {
        std::filesystem::create_directories(mConfigPath.parent_path());
        nlohmann::ordered_json servers = nlohmann::ordered_json::array();
        for (auto const& server : mConfig.servers) {
            servers.push_back(toJson(server));
        }

        nlohmann::ordered_json data{
            {"enabled", mConfig.enabled},
            {"teleportDelay", mConfig.teleportDelay},
            {"requirePermission", mConfig.requirePermission},
            {"permissionNode", mConfig.permissionNode},
            {"effects",
             {
                 {"sound", toJson(mConfig.effects.sound)},
                 {"particle", toJson(mConfig.effects.particle)},
                 {"title", toJson(mConfig.effects.title)},
             }},
            {"servers", servers},
        };

        std::ofstream file(mConfigPath, std::ios::trunc);
        file << data.dump(4);
        return true;
    } catch (std::exception const& error) {
        getSelf().getLogger().error("Failed to save config: {}", error.what());
        return false;
    }
}

void CrossServerTeleport::registerCommands() {
    auto& registrar = ll::command::CommandRegistrar::getServerInstance();
    auto& command   = registrar.getOrCreateCommand("servertp", "打开跨服传送菜单", CommandPermissionLevel::Any);
    command.alias("stp");
    command.overload().execute([this](CommandOrigin const& origin, CommandOutput& output) {
        auto* actor = origin.getEntity();
        if (actor == nullptr || !actor->isPlayer()) {
            output.error("{}该命令只能由玩家执行。", PluginPrefix);
            return;
        }
        showMainMenu(static_cast<Player&>(*actor));
    });
}

void CrossServerTeleport::registerTickListener() {
    if (mTickListener) return;

    auto listener = ll::event::Listener<ll::event::world::ServerLevelTickEvent>::create(
        [this](ll::event::world::ServerLevelTickEvent& event) { onServerTick(event); }
    );
    ll::event::EventBus::getInstance().addListener(listener);
    mTickListener = listener;
}

void CrossServerTeleport::onServerTick(ll::event::world::ServerLevelTickEvent const& event) {
    if (mPendingTeleports.empty()) return;

    auto& level = event.level();
    for (auto it = mPendingTeleports.begin(); it != mPendingTeleports.end();) {
        if (--it->ticksLeft > 0) {
            ++it;
            continue;
        }

        auto* player = level.getPlayer(it->playerName);
        if (player != nullptr) {
            transferPlayer(*player, it->server);
        }
        it = mPendingTeleports.erase(it);
    }
}

void CrossServerTeleport::showMainMenu(Player& player) {
    if (!mConfig.enabled) {
        player.sendMessage(std::string(PluginPrefix) + "§c插件已禁用。");
        return;
    }

    std::vector<ServerConfig> enabledServers;
    std::copy_if(mConfig.servers.begin(), mConfig.servers.end(), std::back_inserter(enabledServers), [](auto const& s) {
        return s.enabled;
    });

    ll::form::SimpleForm form("§l§6跨服传送", "§r欢迎使用跨服传送系统\n§7请选择要前往的服务器");
    for (auto const& server : enabledServers) {
        auto desc = replaceAll(server.description, "{online}", "?");
        form.appendButton(server.name + "\n§r" + desc, server.icon.empty() ? IconServer : server.icon, "path");
    }

    if (player.isOperator()) {
        form.appendButton("§c§l管理设置\n§7服务器和插件配置", IconSettings, "path");
    }

    form.sendTo(player, [this, enabledServers](Player& p, int id, ll::form::FormCancelReason) {
        if (id < 0) return;
        if (id < static_cast<int>(enabledServers.size())) {
            showTeleportConfirm(p, enabledServers[static_cast<size_t>(id)]);
            return;
        }
        if (id == static_cast<int>(enabledServers.size()) && p.isOperator()) {
            openAdminMenu(p);
        }
    });
}

void CrossServerTeleport::showTeleportConfirm(Player& player, ServerConfig server) {
    ll::form::SimpleForm form;
    form.setTitle("§l§e传送确认");

    auto content = "§r确定要传送到:\n\n§l" + server.name + "\n\n§r"
                 + replaceAll(server.description, "{online}", "?") + "\n\n";
    content += "§7传送地址: §e" + server.address + "\n";
    content += "§7传送延迟: §e" + std::to_string(mConfig.teleportDelay) + " §7秒";
    form.setContent(content);
    form.appendButton("§a§l确认传送", IconConfirm, "path");
    form.appendButton("§c§l取消", IconCancel, "path");

    form.sendTo(player, [this, server = std::move(server)](Player& p, int id, ll::form::FormCancelReason) {
        if (id == 0) {
            executeTeleport(p, server);
        } else if (id == 1) {
            showMainMenu(p);
        }
    });
}

void CrossServerTeleport::openAdminMenu(Player& player) {
    if (!player.isOperator()) {
        player.sendMessage(std::string(PluginPrefix) + "§c你没有权限打开管理菜单。");
        return;
    }

    ll::form::SimpleForm form("§l§c跨服传送管理", "§r管理员设置面板");
    form.appendButton("§a§l服务器列表\n§7查看和管理服务器", IconList, "path");
    form.appendButton("§b§l插件设置\n§7配置插件参数", IconSettings, "path");
    form.appendButton("§d§l重载配置\n§7重新加载配置文件", IconReload, "path");
    form.appendButton("§7§l返回", IconBack, "path");

    form.sendTo(player, [this](Player& p, int id, ll::form::FormCancelReason) {
        if (id == 0) showServerList(p);
        if (id == 1) showPluginSettings(p);
        if (id == 2) reloadConfig(p);
        if (id == 3) showMainMenu(p);
    });
}

void CrossServerTeleport::reloadConfig(Player& player) {
    loadConfig();
    player.sendMessage(std::string(PluginPrefix) + "§a配置已重新加载。");
    openAdminMenu(player);
}

void CrossServerTeleport::showServerList(Player& player) {
    ll::form::SimpleForm form;
    form.setTitle("§l§a服务器管理");
    form.setContent(
        "§r当前服务器数量: §e" + std::to_string(mConfig.servers.size()) + "\n§7点击服务器进行编辑或删除"
    );

    for (auto const& server : mConfig.servers) {
        auto status = server.enabled ? "§a●" : "§c●";
        form.appendButton(status + std::string(" ") + server.name + "\n§7" + server.address, server.icon.empty() ? IconServer : server.icon, "path");
    }
    form.appendButton("§a§l+ 添加服务器", IconAdd, "path");
    form.appendButton("§7§l返回", IconBack, "path");

    form.sendTo(player, [this](Player& p, int id, ll::form::FormCancelReason) {
        if (id < 0) return;
        if (id < static_cast<int>(mConfig.servers.size())) {
            showServerOptions(p, static_cast<size_t>(id));
        } else if (id == static_cast<int>(mConfig.servers.size())) {
            showAddServer(p);
        } else {
            openAdminMenu(p);
        }
    });
}

void CrossServerTeleport::showServerOptions(Player& player, size_t index) {
    if (index >= mConfig.servers.size()) {
        player.sendMessage(std::string(PluginPrefix) + "§c服务器不存在。");
        showServerList(player);
        return;
    }

    auto const& server = mConfig.servers[index];
    ll::form::SimpleForm form;
    form.setTitle("§l§b" + server.name);
    form.setContent(
        "§r服务器信息\n\n§7名称: §e" + server.name + "\n§7地址: §e" + server.address
        + "\n§7状态: " + (server.enabled ? "§a启用" : "§c禁用")
        + "\n§7需要权限: " + (server.requirePermission ? "§a是" : "§c否")
    );
    form.appendButton("§e§l编辑服务器", IconEdit, "path");
    form.appendButton("§c§l删除服务器", IconDelete, "path");
    form.appendButton(server.enabled ? "§c§l禁用服务器" : "§a§l启用服务器", IconSettings, "path");
    form.appendButton("§7§l返回", IconBack, "path");

    form.sendTo(player, [this, index](Player& p, int id, ll::form::FormCancelReason) {
        if (id == 0) showEditServer(p, index);
        if (id == 1) confirmDeleteServer(p, index);
        if (id == 2) toggleServerStatus(p, index);
        if (id == 3) showServerList(p);
    });
}

void CrossServerTeleport::toggleServerStatus(Player& player, size_t index) {
    if (index >= mConfig.servers.size()) {
        player.sendMessage(std::string(PluginPrefix) + "§c服务器不存在。");
        showServerList(player);
        return;
    }

    auto& server  = mConfig.servers[index];
    server.enabled = !server.enabled;
    if (saveConfig()) {
        player.sendMessage(std::string(PluginPrefix) + "§a服务器 " + server.name + " 已" + (server.enabled ? "启用" : "禁用"));
    } else {
        player.sendMessage(std::string(PluginPrefix) + "§c操作失败。");
    }
    showServerOptions(player, index);
}

void CrossServerTeleport::confirmDeleteServer(Player& player, size_t index) {
    if (index >= mConfig.servers.size()) {
        player.sendMessage(std::string(PluginPrefix) + "§c服务器不存在。");
        showServerList(player);
        return;
    }

    auto server = mConfig.servers[index];
    ll::form::SimpleForm form("§l§c确认删除", "§r确定要删除服务器吗？\n\n§e" + server.name + "\n§7" + server.address + "\n\n§c此操作不可撤销！");
    form.appendButton("§c§l确认删除", IconDelete, "path");
    form.appendButton("§a§l取消", IconCancel, "path");

    form.sendTo(player, [this, index, server = std::move(server)](Player& p, int id, ll::form::FormCancelReason) {
        if (id == 0 && index < mConfig.servers.size()) {
            mConfig.servers.erase(mConfig.servers.begin() + static_cast<std::ptrdiff_t>(index));
            if (saveConfig()) {
                p.sendMessage(std::string(PluginPrefix) + "§a服务器 " + server.name + " 已删除。");
            } else {
                p.sendMessage(std::string(PluginPrefix) + "§c删除失败。");
            }
            showServerList(p);
        } else {
            showServerOptions(p, index);
        }
    });
}

void CrossServerTeleport::showAddServer(Player& player) {
    ll::form::CustomForm form("§l§a添加服务器");
    form.appendInput("id", "§e服务器ID", "英文标识，如: survival");
    form.appendInput("name", "§b服务器名称", "显示名称，支持颜色代码");
    form.appendInput("address", "§d服务器地址", "格式: 地址:端口");
    form.appendInput("description", "§7服务器描述", "支持颜色代码和\\n换行");
    form.appendToggle("enabled", "§a启用服务器", true);
    form.appendToggle("requirePermission", "§c需要权限", false);

    form.sendTo(player, [this](Player& p, ll::form::CustomFormResult const& data, ll::form::FormCancelReason) {
        if (!data) {
            showServerList(p);
            return;
        }

        auto id                = trim(getValue<std::string>(data, "id", ""));
        auto name              = trim(getValue<std::string>(data, "name", ""));
        auto address           = trim(getValue<std::string>(data, "address", ""));
        auto description       = trim(getValue<std::string>(data, "description", ""));
        auto enabled           = getValue<bool>(data, "enabled", true);
        auto requirePermission = getValue<bool>(data, "requirePermission", false);

        if (id.empty() || name.empty() || address.empty()) {
            p.sendMessage(std::string(PluginPrefix) + "§cID、名称和地址不能为空。");
            showAddServer(p);
            return;
        }
        if (std::any_of(mConfig.servers.begin(), mConfig.servers.end(), [&](auto const& s) { return s.id == id; })) {
            p.sendMessage(std::string(PluginPrefix) + "§c服务器ID已存在。");
            showAddServer(p);
            return;
        }

        mConfig.servers.push_back({
            id,
            name,
            address,
            description.empty() ? "§7" + name : description,
            IconServer,
            enabled,
            requirePermission,
            "crossserverteleport." + id,
        });

        p.sendMessage(std::string(PluginPrefix) + (saveConfig() ? "§a服务器添加成功。" : "§c服务器添加失败。"));
        showServerList(p);
    });
}

void CrossServerTeleport::showEditServer(Player& player, size_t index) {
    if (index >= mConfig.servers.size()) {
        player.sendMessage(std::string(PluginPrefix) + "§c服务器不存在。");
        showServerList(player);
        return;
    }

    auto server = mConfig.servers[index];
    ll::form::CustomForm form("§l§b编辑: " + server.name);
    form.appendInput("id", "§e服务器ID", "英文标识", server.id);
    form.appendInput("name", "§b服务器名称", "显示名称，支持颜色代码", server.name);
    form.appendInput("address", "§d服务器地址", "格式: 地址:端口", server.address);
    form.appendInput("description", "§7服务器描述", "支持颜色代码和\\n换行", server.description);
    form.appendToggle("enabled", "§a启用服务器", server.enabled);
    form.appendToggle("requirePermission", "§c需要权限", server.requirePermission);

    form.sendTo(player, [this, index, server](Player& p, ll::form::CustomFormResult const& data, ll::form::FormCancelReason) {
        if (!data) {
            showServerOptions(p, index);
            return;
        }

        auto id                = trim(getValue<std::string>(data, "id", ""));
        auto name              = trim(getValue<std::string>(data, "name", ""));
        auto address           = trim(getValue<std::string>(data, "address", ""));
        auto description       = trim(getValue<std::string>(data, "description", ""));
        auto enabled           = getValue<bool>(data, "enabled", true);
        auto requirePermission = getValue<bool>(data, "requirePermission", false);

        if (id.empty() || name.empty() || address.empty()) {
            p.sendMessage(std::string(PluginPrefix) + "§cID、名称和地址不能为空。");
            showEditServer(p, index);
            return;
        }
        if (std::any_of(mConfig.servers.begin(), mConfig.servers.end(), [&](auto const& s) {
                return s.id == id && &s != &mConfig.servers[index];
            })) {
            p.sendMessage(std::string(PluginPrefix) + "§c服务器ID已被其他服务器使用。");
            showEditServer(p, index);
            return;
        }

        mConfig.servers[index] = {
            id,
            name,
            address,
            description.empty() ? "§7" + name : description,
            server.icon.empty() ? std::string(IconServer) : server.icon,
            enabled,
            requirePermission,
            "crossserverteleport." + id,
        };

        p.sendMessage(std::string(PluginPrefix) + (saveConfig() ? "§a服务器修改成功。" : "§c服务器修改失败。"));
        showServerOptions(p, index);
    });
}

void CrossServerTeleport::showPluginSettings(Player& player) {
    ll::form::CustomForm form("§l§b插件设置");
    form.appendToggle("enabled", "§e插件开关", mConfig.enabled);
    form.appendSlider("teleportDelay", "§b传送延迟(秒)", 0, 10, 1, mConfig.teleportDelay);
    form.appendToggle("requirePermission", "§d全局需要权限", mConfig.requirePermission);
    form.appendInput("permissionNode", "§7权限节点", "权限节点", mConfig.permissionNode);

    form.sendTo(player, [this](Player& p, ll::form::CustomFormResult const& data, ll::form::FormCancelReason) {
        if (!data) {
            openAdminMenu(p);
            return;
        }

        mConfig.enabled           = getValue<bool>(data, "enabled", true);
        mConfig.teleportDelay     = std::clamp(getValue<int>(data, "teleportDelay", 3), 0, 10);
        mConfig.requirePermission = getValue<bool>(data, "requirePermission", false);
        mConfig.permissionNode    = trim(getValue<std::string>(data, "permissionNode", "crossserverteleport.use"));
        if (mConfig.permissionNode.empty()) mConfig.permissionNode = "crossserverteleport.use";

        p.sendMessage(std::string(PluginPrefix) + (saveConfig() ? "§a插件设置已保存。" : "§c插件设置保存失败。"));
        openAdminMenu(p);
    });
}

void CrossServerTeleport::executeTeleport(Player& player, ServerConfig server) {
    if (mConfig.requirePermission && !player.isOperator()) {
        player.sendMessage(std::string(PluginPrefix) + "§c你没有权限使用跨服传送。");
        return;
    }
    if (server.requirePermission && !player.isOperator()) {
        player.sendMessage(std::string(PluginPrefix) + "§c你没有权限传送到 " + server.name);
        return;
    }

    runTeleportEffects(player, server);
    player.sendMessage(std::string(PluginPrefix) + "§e正在准备传送到 " + server.name + "...");

    if (mConfig.teleportDelay <= 0) {
        transferPlayer(player, server);
        return;
    }

    player.sendMessage(
        std::string(PluginPrefix) + "§7传送将在 §e" + std::to_string(mConfig.teleportDelay) + " §7秒后开始。"
    );
    mPendingTeleports.push_back({player.getRealName(), std::move(server), mConfig.teleportDelay * 20});
}

void CrossServerTeleport::runTeleportEffects(Player& player, ServerConfig const& server) {
    (void)player;
    (void)server;
    // Do not execute server commands from a form callback here. On LeviLamina 26.10.14 / BDS 1.26.10
    // constructing a synthetic ServerCommandOrigin in this path can crash the server.
}

void CrossServerTeleport::transferPlayer(Player& player, ServerConfig const& server) {
    auto [address, port] = parseAddress(server.address);
    if (address.empty()) {
        player.sendMessage(std::string(PluginPrefix) + "§c服务器地址配置错误。");
        return;
    }

    TransferPacket packet(address, port, false);
    player.sendNetworkPacket(packet);
    getSelf().getLogger().info("Player {} transferred to {} ({})", player.getRealName(), server.name, server.address);
}

} // namespace cross_server_teleport

LL_REGISTER_MOD(cross_server_teleport::CrossServerTeleport, cross_server_teleport::CrossServerTeleport::getInstance());
