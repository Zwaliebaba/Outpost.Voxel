#include "pch.h"

#include "ClientSession.h"
#include "TestSupport.h"

#include "Composite.h"
#include "Hash.h"
#include "LoopbackTransport.h"
#include "Message.h"
#include "NvfModel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

using NeuronClient::ClientSession;
using NeuronClient::SessionError;

// The models GameData holds, as the game's server names them.
constexpr std::array<const char*, 3> MODEL_NAMES{"MilitaryStation", "CapitalShip", "Frigate"};

// The welcome's one side, and a payload the engine carries unread (Design/ADR/ADR-029).
constexpr NeuronCore::SideColor SIDE{40, 120, 220};
constexpr std::array<std::uint8_t, 3> PAYLOAD{5, 6, 7};

constexpr NeuronCore::WorldSettings SETTINGS{
  {0.0f, 1.0f, 0.0f}, {0.7f, 0.7f, 0.7f}, 0.005f, {0.05f, 0.05f, 0.05f}, {0.05f, 0.05f, 0.05f}, 7u, {0.0f, 0.0f, 0.0f, 1.0f}};

// A server's manifest of GameData's models: each one's name and its file's hash.
[[nodiscard]] std::vector<NeuronCore::ManifestEntry> GameDataManifest()
{
  std::vector<NeuronCore::ManifestEntry> manifest;
  for (const char* name : MODEL_NAMES)
  {
    const auto bytes = NeuronCore::ReadNvfFile(GameDataDirectory() / (std::string(name) + ".nvf"));
    Assert::IsTrue(bytes.has_value(), L"GameData holds the model");
    manifest.push_back({name, NeuronCore::Fnv1aHash64(bytes.value_or(std::vector<std::uint8_t>{}))});
  }
  return manifest;
}

// A welcome that names _manifest's models, each alone as a composite, and one side.
[[nodiscard]] NeuronCore::Welcome WelcomeOf(std::vector<NeuronCore::ManifestEntry> _manifest)
{
  std::vector<NeuronCore::CompositeModel> composites = NeuronCore::SingleModelComposites(_manifest.size());
  return {NeuronCore::PROTOCOL_VERSION,    30, 0, SETTINGS, std::move(_manifest), std::move(composites), {SIDE}, 1,
          {PAYLOAD.begin(), PAYLOAD.end()}};
}

// A client session over a loopback, and the server's end of it, which the test speaks for.
struct Link
{
  std::unique_ptr<NeuronCore::Transport> server;
  std::unique_ptr<ClientSession> session;
};

[[nodiscard]] Link Connect(const std::filesystem::path& _modelDirectory)
{
  NeuronCore::LoopbackPair pair = NeuronCore::MakeLoopbackPair();
  return {std::move(pair.server), std::make_unique<ClientSession>(std::move(pair.client), _modelDirectory)};
}

void Send(NeuronCore::Transport& _transport, const NeuronCore::Message& _message)
{
  Assert::IsTrue(_transport.Send(NeuronCore::EncodeMessage(_message)), L"the server's end is open");
}

// The next message the server's end has received, decoded.
[[nodiscard]] NeuronCore::Message ReceiveAtServer(NeuronCore::Transport& _server)
{
  const std::optional<std::vector<std::uint8_t>> bytes = _server.Receive();
  Assert::IsTrue(bytes.has_value(), L"the client sent a message");
  const auto message = NeuronCore::DecodeMessage(bytes.value_or(std::vector<std::uint8_t>{}), {MODEL_NAMES.size(), 1});
  Assert::IsTrue(message.has_value(), L"the client's message decodes");
  return message.value_or(NeuronCore::Message{});
}

// Polls _session, which must refuse, and checks the refusal and what it says; then that it stays refused, that the
// server's end sees the link closed, and that nothing more can be sent.
void ExpectRefusal(Link& _link, NeuronClient::SessionRefusal _refusal, const char* _detail)
{
  const std::expected<void, SessionError> polled = _link.session->Poll(1.0);
  Assert::IsFalse(polled.has_value(), L"the session refused");
  const SessionError error = polled.error_or(SessionError{});
  Assert::AreEqual(NeuronClient::SessionRefusalName(_refusal), NeuronClient::SessionRefusalName(error.refusal));
  Assert::AreEqual(_detail, error.detail.c_str());
  Assert::AreEqual((std::string(NeuronClient::SessionRefusalName(_refusal)) + ": " + _detail).c_str(),
                   NeuronClient::DescribeSessionError(error).c_str(), false, L"described as a message says it");
  const std::expected<void, SessionError> again = _link.session->Poll(2.0);
  Assert::IsFalse(again.has_value(), L"and stays refused");
  Assert::AreEqual(NeuronClient::SessionRefusalName(_refusal), NeuronClient::SessionRefusalName(again.error_or(SessionError{}).refusal),
                   false, L"for the same reason");
  Assert::AreEqual(_detail, again.error_or(SessionError{}).detail.c_str(), false, L"in the same words");
  Assert::IsFalse(_link.server->IsOpen(), L"the session closed its transport");
  Assert::IsFalse(_link.session->Send({NeuronCore::CommandKind::Pause, 0}), L"nothing more is sent");
}

} // namespace

// Design/Archive/SpaceScene.md §6.1 and §6.2: the client's handshake, its models by name and hash, its snapshots, its commands,
// and every refusal by name. The test speaks for the server, byte for byte.
TEST_CLASS(ClientSessionTests)
{
public:
  TEST_METHOD(SaysHelloAndLoadsTheModelsTheWelcomeNames)
  {
    Link link = Connect(GameDataDirectory());
    const NeuronCore::Message hello = ReceiveAtServer(*link.server);
    Assert::IsTrue(std::holds_alternative<NeuronCore::Hello>(hello), L"a Hello first");
    Assert::AreEqual(NeuronCore::PROTOCOL_VERSION, std::get<NeuronCore::Hello>(hello).protocolVersion);
    Assert::IsTrue(link.session->Poll(0.0).has_value(), L"nothing has come yet");
    Assert::IsFalse(link.session->IsWelcomed());
    Assert::IsTrue(link.session->Side() == NeuronCore::OBSERVER_SIDE, L"no side before the welcome");

    Send(*link.server, WelcomeOf(GameDataManifest()));
    Send(*link.server,
         NeuronCore::Snapshot{1, 1, false, {{9, 2, 1, {1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 60.0f}}}, {}, {}});
    Assert::IsTrue(link.session->Poll(0.5).has_value(), L"welcomed");
    Assert::IsTrue(link.session->IsWelcomed());
    Assert::AreEqual(std::size_t{3}, link.session->Models().size());
    Assert::AreEqual(std::size_t{225048}, link.session->Models()[0].records.size(), L"the station, whole");
    Assert::AreEqual(std::size_t{10747}, link.session->Models()[1].records.size(), L"the capital ship");
    Assert::AreEqual(std::size_t{1181}, link.session->Models()[2].records.size(), L"the frigate");
    Assert::AreEqual(7u, link.session->Settings().skySeed, L"the welcome's settings");
    Assert::AreEqual(30u, link.session->Buffer().TickRate());
    Assert::AreEqual(std::uint64_t{1}, link.session->Buffer().Newest().tick, L"the snapshot in the buffer");
    Assert::AreEqual(9u, link.session->Buffer().Newest().entities.front().id);
    Assert::AreEqual(std::size_t{3}, link.session->Composites().size(), L"the welcome's composites");
    Assert::AreEqual(std::uint16_t{2}, link.session->Composites()[2].components.front().model);
    Assert::AreEqual(std::size_t{1}, link.session->Sides().size(), L"its side");
    Assert::IsTrue(link.session->Sides().front().blue == SIDE.blue, L"its color");
    Assert::IsTrue(link.session->Side() == 1, L"and the side the session plays (Design/ADR/ADR-032)");
    Assert::AreEqual(PAYLOAD.size(), link.session->WelcomePayload().size(), L"its payload, unread");
    Assert::IsTrue(link.session->WelcomePayload().back() == PAYLOAD.back(), L"byte for byte");
  }

  TEST_METHOD(SendsItsCommands)
  {
    Link link = Connect(GameDataDirectory());
    static_cast<void>(ReceiveAtServer(*link.server));
    Send(*link.server, WelcomeOf(GameDataManifest()));
    Assert::IsTrue(link.session->Poll(0.0).has_value());
    Assert::IsTrue(link.session->Send({NeuronCore::CommandKind::Detonate, 7}));
    Assert::IsTrue(link.session->Send({NeuronCore::CommandKind::Pause, 0}));
    const NeuronCore::Message detonate = ReceiveAtServer(*link.server);
    Assert::IsTrue(std::holds_alternative<NeuronCore::Command>(detonate), L"a command");
    Assert::IsTrue(std::get<NeuronCore::Command>(detonate).kind == NeuronCore::CommandKind::Detonate, L"detonate");
    Assert::AreEqual(7u, std::get<NeuronCore::Command>(detonate).entity);
    const NeuronCore::Message pause = ReceiveAtServer(*link.server);
    Assert::IsTrue(std::get<NeuronCore::Command>(pause).kind == NeuronCore::CommandKind::Pause, L"then pause, in order");
  }

  TEST_METHOD(RefusesAModelItCannotRead)
  {
    Link link = Connect(GameDataDirectory());
    std::vector<NeuronCore::ManifestEntry> manifest = GameDataManifest();
    manifest.push_back({"NoSuchModel", 1});
    Send(*link.server, WelcomeOf(std::move(manifest)));
    ExpectRefusal(link, NeuronClient::SessionRefusal::ModelNotLoaded, "NoSuchModel.nvf: FileNotFound");
    Assert::IsFalse(link.session->IsWelcomed(), L"a welcome it cannot keep is not kept");
  }

  TEST_METHOD(RefusesAModelItsReaderRefuses)
  {
    // A file of the right name and hash that is not an .nvf file.
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "OutpostClientSessionTests";
    std::filesystem::create_directories(directory);
    const std::string text = "not an nvf file";
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    {
      std::ofstream file(directory / "Garbage.nvf", std::ios::binary | std::ios::trunc);
      file.write(text.data(), static_cast<std::streamsize>(text.size()));
      Assert::IsTrue(file.good(), L"writing the file");
    }
    Link link = Connect(directory);
    Send(*link.server, WelcomeOf({{"Garbage", NeuronCore::Fnv1aHash64(bytes)}}));
    ExpectRefusal(link, NeuronClient::SessionRefusal::ModelNotLoaded, "Garbage.nvf: NotAnNvfFile");
    std::filesystem::remove_all(directory);
  }

  TEST_METHOD(RefusesAModelWhoseHashDiffers)
  {
    Link link = Connect(GameDataDirectory());
    std::vector<NeuronCore::ManifestEntry> manifest = GameDataManifest();
    const std::uint64_t hash = manifest[1].hash;
    manifest[1].hash = 0x0123456789ABCDEFull;
    Send(*link.server, WelcomeOf(std::move(manifest)));
    const std::string detail = std::format("CapitalShip.nvf: this file's hash is {:016x}, and the server's 0123456789abcdef", hash);
    ExpectRefusal(link, NeuronClient::SessionRefusal::ModelMismatch, detail.c_str());
  }

  TEST_METHOD(RefusesAnotherProtocol)
  {
    Link link = Connect(GameDataDirectory());
    NeuronCore::Welcome welcome = WelcomeOf(GameDataManifest());
    welcome.protocolVersion = NeuronCore::PROTOCOL_VERSION + 1;
    Send(*link.server, welcome);
    const std::string detail =
      std::format("the server speaks protocol {}, and this client {}", NeuronCore::PROTOCOL_VERSION + 1, NeuronCore::PROTOCOL_VERSION);
    ExpectRefusal(link, NeuronClient::SessionRefusal::WrongProtocol, detail.c_str());
  }

  TEST_METHOD(RefusesWhatAClientNeverReceives)
  {
    {
      Link link = Connect(GameDataDirectory());
      Send(*link.server, NeuronCore::Hello{NeuronCore::PROTOCOL_VERSION});
      ExpectRefusal(link, NeuronClient::SessionRefusal::BadMessage, "a message only a server receives");
    }
    {
      Link link = Connect(GameDataDirectory());
      Send(*link.server, WelcomeOf(GameDataManifest()));
      Send(*link.server, NeuronCore::Command{NeuronCore::CommandKind::Resume, 0});
      ExpectRefusal(link, NeuronClient::SessionRefusal::BadMessage, "a message only a server receives");
    }
    {
      Link link = Connect(GameDataDirectory());
      Send(*link.server, NeuronCore::Snapshot{1, 1, false, {}, {}, {}});
      ExpectRefusal(link, NeuronClient::SessionRefusal::BadMessage, "a Snapshot before the Welcome");
    }
    {
      Link link = Connect(GameDataDirectory());
      Send(*link.server, WelcomeOf(GameDataManifest()));
      Send(*link.server, WelcomeOf(GameDataManifest()));
      ExpectRefusal(link, NeuronClient::SessionRefusal::BadMessage, "a second Welcome");
    }
    {
      // Bytes that do not decode are refused by the decoder's name for them.
      Link link = Connect(GameDataDirectory());
      const std::array<std::uint8_t, 3> bytes{1, 2, 3};
      Assert::IsTrue(link.server->Send(bytes));
      ExpectRefusal(link, NeuronClient::SessionRefusal::BadMessage, "Truncated");
    }
  }

  TEST_METHOD(LearnsThatTheServerClosedIt)
  {
    // What the server sent before it closed the link is taken first.
    Link link = Connect(GameDataDirectory());
    Send(*link.server, WelcomeOf(GameDataManifest()));
    Send(*link.server, NeuronCore::Snapshot{1, 1, false, {}, {}, {}});
    link.server->Close();
    ExpectRefusal(link, NeuronClient::SessionRefusal::Closed, "the server closed the session");
    Assert::IsTrue(link.session->IsWelcomed(), L"the welcome was taken");
    Assert::AreEqual(std::uint64_t{1}, link.session->Buffer().Newest().tick, L"and the snapshot");
  }
};

} // namespace NeuronClientTests
