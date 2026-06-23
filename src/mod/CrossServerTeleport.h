#pragma once

#include "ll/api/mod/NativeMod.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ll::event {
class ListenerBase;
}
namespace ll::event::inline world {
class ServerLevelTickEvent;
}

class Player;
class CommandOrigin;
class CommandOutput;

namespace cross_server_teleport {

class CrossServerTeleport {

public:
    static CrossServerTeleport& getInstance();

    CrossServerTeleport() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    /// @return True if the mod is loaded successfully.
    bool load();

    /// @return True if the mod is enabled successfully.
    bool enable();

    /// @return True if the mod is disabled successfully.
    bool disable();

    void showMainMenu(Player& player);

    struct SoundConfig {
        bool        enabled = true;
        std::string soundId = "mob.endermen.portal";
        double      volume  = 1.0;
        double      pitch   = 1.0;
    };

    struct ParticleConfig {
        bool        enabled    = true;
        std::string particleId = "minecraft:portal";
        int         count      = 100;
    };

    struct TitleConfig {
        bool        enabled   = true;
        std::string mainTitle = "§d§l正在传送...";
        std::string subtitle  = "§e目标: {server}";
        int         fadeIn    = 10;
        int         stay      = 40;
        int         fadeOut   = 10;
    };

    struct EffectsConfig {
        SoundConfig    sound;
        ParticleConfig particle;
        TitleConfig    title;
    };

    struct ServerConfig {
        std::string id;
        std::string name;
        std::string address;
        std::string description;
        std::string icon;
        bool        enabled           = true;
        bool        requirePermission = false;
        std::string permissionNode;
    };

    struct Config {
        bool                      enabled           = true;
        int                       teleportDelay     = 3;
        bool                      requirePermission = false;
        std::string               permissionNode    = "crossserverteleport.use";
        EffectsConfig             effects;
        std::vector<ServerConfig> servers;
    };

private:
    struct PendingTeleport {
        std::string  playerName;
        ServerConfig server;
        int          ticksLeft;
    };

    ll::mod::NativeMod& mSelf;
    Config              mConfig;
    std::filesystem::path mConfigPath;
    std::vector<PendingTeleport> mPendingTeleports;
    std::shared_ptr<ll::event::ListenerBase> mTickListener;

    void ensureDefaultServers();
    void loadConfig();
    bool saveConfig() const;
    void registerCommands();
    void registerTickListener();
    void onServerTick(ll::event::world::ServerLevelTickEvent const& event);

    void showTeleportConfirm(Player& player, ServerConfig server);
    void openAdminMenu(Player& player);
    void reloadConfig(Player& player);
    void showServerList(Player& player);
    void showServerOptions(Player& player, size_t index);
    void showAddServer(Player& player);
    void showEditServer(Player& player, size_t index);
    void confirmDeleteServer(Player& player, size_t index);
    void toggleServerStatus(Player& player, size_t index);
    void showPluginSettings(Player& player);

    void executeTeleport(Player& player, ServerConfig server);
    void runTeleportEffects(Player& player, ServerConfig const& server);
    void transferPlayer(Player& player, ServerConfig const& server);
};

} // namespace cross_server_teleport
