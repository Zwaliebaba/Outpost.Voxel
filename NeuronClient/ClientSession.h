#pragma once

#include "SnapshotBuffer.h"

#include "Message.h"
#include "Transport.h"
#include "VoxModel.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace NeuronClient
{

// Why a session ended (Design/SpaceScene.md §6.2). The client refuses by name, as the server and the readers do.
enum class SessionRefusal : std::uint8_t
{
  Closed,         // the server closed the session: it refused the client, or it stopped
  BadMessage,     // bytes that do not decode, or a message a client never receives; the detail says which
  WrongProtocol,  // a welcome of another protocol
  ModelNotLoaded, // a model the welcome names whose file the client cannot read, or whose reader refused it
  ModelMismatch   // a model whose file's hash is not the one the welcome gives
};

[[nodiscard]] const char* SessionRefusalName(SessionRefusal _refusal) noexcept;

struct SessionError
{
  SessionRefusal refusal;
  std::string detail; // which message or model, and why
};

// The client's end of a session with a server (§6.1, §6.2). It says Hello, loads every model the welcome names from its
// own copy of the model's file, refusing one whose hash differs, and then feeds each snapshot into its buffer. What the
// server knows reaches it only as these messages, through its transport (AGENTS.md R18).
class ClientSession
{
public:
  // Says Hello through _transport. The welcome's models are read from _modelDirectory, each from <name>.vox.
  ClientSession(std::unique_ptr<NeuronCore::Transport> _transport, std::filesystem::path _modelDirectory);

  // Takes every message waiting, each as having arrived at _arrivalSeconds on the client's clock. On a refusal the
  // session closes its transport, and every later call gives the same refusal.
  [[nodiscard]] std::expected<void, SessionError> Poll(double _arrivalSeconds);

  // Sends _command. False once the session has ended.
  bool Send(const NeuronCore::Command& _command);

  [[nodiscard]] bool IsWelcomed() const noexcept
  {
    return m_welcome.has_value();
  }

  // What the welcome said, once it has come: the world's settings and the models' names and hashes.
  [[nodiscard]] const NeuronCore::WorldSettings& Settings() const noexcept
  {
    return m_welcome->settings;
  }

  [[nodiscard]] std::span<const NeuronCore::ManifestEntry> Manifest() const noexcept
  {
    return m_welcome->manifest;
  }

  // The models the welcome names, in its order, which the snapshots' model indices name; empty until it has come.
  [[nodiscard]] std::span<const NeuronCore::VoxModel> Models() const noexcept
  {
    return m_models;
  }

  // The snapshots, once the welcome has come.
  [[nodiscard]] SnapshotBuffer& Buffer() noexcept
  {
    return *m_buffer;
  }

  [[nodiscard]] const SnapshotBuffer& Buffer() const noexcept
  {
    return *m_buffer;
  }

private:
  [[nodiscard]] std::unexpected<SessionError> Refuse(SessionRefusal _refusal, std::string _detail);

  std::unique_ptr<NeuronCore::Transport> m_transport;
  std::filesystem::path m_modelDirectory;
  std::optional<NeuronCore::Welcome> m_welcome;
  std::vector<NeuronCore::VoxModel> m_models;
  std::optional<SnapshotBuffer> m_buffer;
  std::optional<SessionError> m_error;
};

} // namespace NeuronClient
