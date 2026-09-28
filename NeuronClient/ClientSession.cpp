#include "pch.h"

#include "ClientSession.h"

#include "Hash.h"

#include <format>
#include <utility>
#include <variant>

namespace NeuronClient
{

const char* SessionRefusalName(SessionRefusal _refusal) noexcept
{
  switch (_refusal)
  {
  case SessionRefusal::Closed:
    return "Closed";
  case SessionRefusal::BadMessage:
    return "BadMessage";
  case SessionRefusal::WrongProtocol:
    return "WrongProtocol";
  case SessionRefusal::ModelNotLoaded:
    return "ModelNotLoaded";
  case SessionRefusal::ModelMismatch:
    return "ModelMismatch";
  }
  return "Unknown";
}

ClientSession::ClientSession(std::unique_ptr<NeuronCore::Transport> _transport, std::filesystem::path _modelDirectory)
  : m_transport(std::move(_transport)),
    m_modelDirectory(std::move(_modelDirectory))
{
  // A transport already closed says so at the first Poll.
  static_cast<void>(m_transport->Send(NeuronCore::EncodeMessage(NeuronCore::Hello{NeuronCore::PROTOCOL_VERSION})));
}

std::expected<void, SessionError> ClientSession::Poll(double _arrivalSeconds)
{
  if (m_error)
  {
    return std::unexpected(*m_error);
  }
  while (std::optional<std::vector<std::uint8_t>> bytes = m_transport->Receive())
  {
    std::expected<NeuronCore::Message, NeuronCore::ProtocolError> message = NeuronCore::DecodeMessage(*bytes, m_models.size());
    if (!message)
    {
      return Refuse(SessionRefusal::BadMessage, NeuronCore::ProtocolErrorName(message.error()));
    }
    if (auto* snapshot = std::get_if<NeuronCore::Snapshot>(&*message); snapshot != nullptr && m_buffer)
    {
      m_buffer->Add(std::move(*snapshot), _arrivalSeconds);
      continue;
    }
    const auto* welcome = std::get_if<NeuronCore::Welcome>(&*message);
    if (welcome == nullptr || m_welcome)
    {
      return Refuse(SessionRefusal::BadMessage, welcome != nullptr ? "a second Welcome"
                                                : std::holds_alternative<NeuronCore::Snapshot>(*message)
                                                  ? "a Snapshot before the Welcome"
                                                  : "a message only a server receives");
    }
    if (welcome->protocolVersion != NeuronCore::PROTOCOL_VERSION)
    {
      return Refuse(SessionRefusal::WrongProtocol, std::format("the server speaks protocol {}, and this client {}",
                                                               welcome->protocolVersion, NeuronCore::PROTOCOL_VERSION));
    }
    // Every model the welcome names, from the client's own copy of its file, before anything is drawn (§6.2).
    std::vector<NeuronCore::VoxModel> models;
    models.reserve(welcome->manifest.size());
    for (const NeuronCore::ManifestEntry& entry : welcome->manifest)
    {
      const std::string file = entry.name + ".vox";
      const std::expected<std::vector<std::uint8_t>, NeuronCore::VoxError> contents = NeuronCore::ReadVoxFile(m_modelDirectory / file);
      if (!contents)
      {
        return Refuse(SessionRefusal::ModelNotLoaded, file + ": " + NeuronCore::VoxErrorName(contents.error()));
      }
      const std::uint64_t hash = NeuronCore::Fnv1aHash64(*contents);
      if (hash != entry.hash)
      {
        return Refuse(SessionRefusal::ModelMismatch,
                      std::format("{}: this file's hash is {:016x}, and the server's {:016x}", file, hash, entry.hash));
      }
      std::expected<NeuronCore::VoxModel, NeuronCore::VoxError> model = NeuronCore::ParseVoxModel(*contents);
      if (!model)
      {
        return Refuse(SessionRefusal::ModelNotLoaded, file + ": " + NeuronCore::VoxErrorName(model.error()));
      }
      models.push_back(std::move(*model));
    }
    m_models = std::move(models);
    m_buffer.emplace(welcome->tickRate);
    m_welcome = *welcome;
  }
  if (!m_transport->IsOpen())
  {
    return Refuse(SessionRefusal::Closed, "the server closed the session");
  }
  return {};
}

bool ClientSession::Send(const NeuronCore::Command& _command)
{
  return !m_error && m_transport->Send(NeuronCore::EncodeMessage(_command));
}

std::unexpected<SessionError> ClientSession::Refuse(SessionRefusal _refusal, std::string _detail)
{
  m_error = SessionError{_refusal, std::move(_detail)};
  m_transport->Close();
  return std::unexpected(*m_error);
}

} // namespace NeuronClient
