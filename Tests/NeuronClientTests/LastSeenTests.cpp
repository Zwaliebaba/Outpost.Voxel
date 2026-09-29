#include "pch.h"

#include "LastSeen.h"
#include "SnapshotBuffer.h"

#include "Float3.h"
#include "Message.h"
#include "Quaternion.h"

#include <cstdint>
#include <optional>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

using NeuronClient::LastSeen;
using NeuronClient::SampledEntity;
using NeuronClient::WorldSample;

// The welcome's composites: a structure, which is remembered out of sight, and a ship, which is not.
constexpr std::uint16_t STRUCTURE = 0;
constexpr std::uint16_t SHIP = 1;

constexpr std::uint32_t CORE = 3;
constexpr std::uint32_t OTHER_CORE = 5;
constexpr std::uint32_t GUNSHIP = 4;

[[nodiscard]] SampledEntity Entity(std::uint32_t _id, std::uint16_t _composite, float _x)
{
  return {_id, _composite, 2, {_x, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, std::nullopt};
}

[[nodiscard]] std::vector<std::uint32_t> IdsOf(const std::vector<SampledEntity>& _entities)
{
  std::vector<std::uint32_t> ids;
  ids.reserve(_entities.size());
  for (const SampledEntity& entity : _entities)
  {
    ids.push_back(entity.id);
  }
  return ids;
}

} // namespace

// Design/ADR/ADR-032: the client remembers the structures it has seen, as it last saw them, while its snapshots do not
// hold them, and nothing else.
TEST_CLASS(LastSeenTests)
{
public:
  TEST_METHOD(RemembersAStructureAsLastSeen)
  {
    LastSeen lastSeen({true, false});
    const WorldSample both{1.0, false, {Entity(CORE, STRUCTURE, 10.0f), Entity(GUNSHIP, SHIP, 20.0f)}};
    lastSeen.See(both);
    Assert::IsTrue(lastSeen.Remembered(both).empty(), L"nothing is remembered while it is seen");

    const WorldSample neither{2.0, false, {}};
    lastSeen.See(neither);
    const std::vector<SampledEntity> remembered = lastSeen.Remembered(neither);
    Assert::IsTrue(IdsOf(remembered) == std::vector<std::uint32_t>{CORE}, L"the structure out of sight, and not the ship");
    Assert::AreEqual(10.0f, remembered.front().position.x, L"where it was last seen");

    const WorldSample moved{3.0, false, {Entity(CORE, STRUCTURE, 15.0f)}};
    lastSeen.See(moved);
    Assert::IsTrue(lastSeen.Remembered(moved).empty(), L"seen again, it is drawn as it is");
    lastSeen.See(neither);
    Assert::AreEqual(15.0f, lastSeen.Remembered(neither).front().position.x, L"and remembered where it was seen last");
  }

  // Structures are remembered each on its own, in the order of their ids, whenever each left; a composite the welcome
  // does not mark, or does not name, is never remembered.
  TEST_METHOD(RemembersEachStructureItHasSeen)
  {
    LastSeen lastSeen({true, false});
    const WorldSample later{1.0, false, {Entity(OTHER_CORE, STRUCTURE, 50.0f), Entity(9, 7, 0.0f)}};
    lastSeen.See(later);
    const WorldSample first{2.0, false, {Entity(CORE, STRUCTURE, 30.0f)}};
    lastSeen.See(first);
    Assert::IsTrue(IdsOf(lastSeen.Remembered(first)) == std::vector<std::uint32_t>{OTHER_CORE}, L"the one out of sight");
    const WorldSample none{3.0, false, {}};
    Assert::IsTrue(IdsOf(lastSeen.Remembered(none)) == std::vector<std::uint32_t>{CORE, OTHER_CORE}, L"both, in the order of their ids");

    LastSeen blind({});
    blind.See(later);
    Assert::IsTrue(blind.Remembered(none).empty(), L"nothing marked, nothing remembered");
  }

  // Debris is remembered as it was last drawn, still: the time since its event stops where the side lost sight of it.
  TEST_METHOD(RemembersDebrisAsLastDrawn)
  {
    LastSeen lastSeen({true});
    SampledEntity wreck = Entity(CORE, STRUCTURE, 0.0f);
    wreck.detonation = NeuronClient::SampledDetonation{{CORE, 17, 40, {0.0f, 0.0f, 0.0f}}, 1.5f};
    lastSeen.See({1.0, false, {wreck}});
    const WorldSample none{2.0, false, {}};
    const std::vector<SampledEntity> remembered = lastSeen.Remembered(none);
    Assert::IsTrue(remembered.size() == 1 && remembered.front().detonation.has_value() && remembered.front().detonation->seconds == 1.5f,
                   L"its debris, at the time it was last drawn");
  }
};

} // namespace NeuronClientTests
