#include "pch.h"

#include "Skirmish.h"
#include "TestSupport.h"

#include "Orders.h"
#include "SkirmishLayout.h"

#include "RigidTransform.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{
namespace
{

constexpr std::uint32_t TICK_RATE = 30;

// The plan's smoke check (Design/MvpPlan.md, phase 5; Design/ADR/ADR-035): duels at equal cost among the three combat
// designs, a gunship against a lancer, and a cruiser, whose price is 7.6 gunships' or 7.9 lancers', against eight of
// either, from three ranges apart, on each seed and from both starts.
struct Pairing
{
  std::string_view first;
  std::uint32_t firstCount;
  std::string_view second;
  std::uint32_t secondCount;
};

constexpr std::array<Pairing, 3> PAIRINGS{{{"Gunship", 1, "Lancer", 1}, {"Cruiser", 1, "Gunship", 8}, {"Cruiser", 1, "Lancer", 8}}};
constexpr std::array<std::int32_t, 3> RANGES{200, 400, 600};

// The plan's seeds, which the bar is held to.
constexpr std::uint32_t FULL_SEEDS = 20;

// A duel that outlasts this has no winner.
constexpr std::uint64_t LONGEST_TICKS = std::uint64_t{240} * TICK_RATE;

// The seeds the check runs, from the environment variable OUTPOST_SMOKE_SEEDS: 1 when it is not set, which CI runs to
// keep the harness working, and 20 for the plan's check, which the phase's pull request tabulates.
[[nodiscard]] std::uint32_t SmokeSeeds()
{
  std::string value;
#if defined(_MSC_VER)
  char* buffer = nullptr;
  std::size_t length = 0;
  if (_dupenv_s(&buffer, &length, "OUTPOST_SMOKE_SEEDS") == 0 && buffer != nullptr)
  {
    value = buffer;
  }
  std::free(buffer);
#else
  if (const char* found = std::getenv("OUTPOST_SMOKE_SEEDS"))
  {
    value = found;
  }
#endif
  const unsigned long seeds = value.empty() ? 1ul : std::strtoul(value.c_str(), nullptr, 10);
  return seeds == 0 ? 1u : static_cast<std::uint32_t>(seeds);
}

struct Outcome
{
  int winner; // 1 for the pairing's first design, 2 for its second, 0 for neither
  double seconds;
  std::uint32_t commandLosses;
  std::uint32_t reactorLosses;
};

// One duel of _pairing from _range apart on _seed. The side that starts first, at -x on side 1, is the pairing's first
// design, or with _secondFirst its second, which then takes the first ids. A lone ship attacks the other lone ship; a
// group attacks the lone ship, which attack-moves through the group's start.
[[nodiscard]] Outcome Fight(const Pairing& _pairing, std::int32_t _range, std::uint32_t _seed, bool _secondFirst)
{
  const std::string_view westDesign = _secondFirst ? _pairing.second : _pairing.first;
  const std::uint32_t westCount = _secondFirst ? _pairing.secondCount : _pairing.firstCount;
  const std::string_view eastDesign = _secondFirst ? _pairing.first : _pairing.second;
  const std::uint32_t eastCount = _secondFirst ? _pairing.firstCount : _pairing.secondCount;
  GameCore::SkirmishLayout layout;
  // Each side in a line across the way to the other, 40 units apart; side 2's the half turn of side 1's places.
  const auto place = [&layout, _range](std::string_view _design, std::uint32_t _count, std::uint8_t _side)
  {
    for (std::uint32_t unit = 0; unit < _count; ++unit)
    {
      const std::int32_t z = static_cast<std::int32_t>(unit) * 40 - static_cast<std::int32_t>(_count - 1) * 20;
      const GameCore::Anchor west{{-_range / 2, 0, z}, GameCore::FACING_SIDE_2};
      layout.units.push_back({_design, _side, _side == 1 ? west : GameCore::HalfTurn(west)});
    }
  };
  place(westDesign, westCount, 1);
  place(eastDesign, eastCount, 2);
  const auto skirmish = MakeStagedSkirmish({.seed = _seed, .tickRate = TICK_RATE}, layout);
  std::vector<std::uint32_t> west;
  std::vector<std::uint32_t> east;
  for (std::uint32_t id = 1; id <= westCount + eastCount; ++id)
  {
    (id <= westCount ? west : east).push_back(id);
  }
  const auto order = [&skirmish](std::uint8_t _side, const GameCore::Order& _order)
  { skirmish->ApplyGameCommand(GameCore::EncodeOrder(_order), _side); };
  const auto engage =
    [&order](std::uint8_t _side, const std::vector<std::uint32_t>& _ships, const std::vector<std::uint32_t>& _enemies, float _enemyX)
  {
    if (_enemies.size() == 1)
    {
      order(_side, {GameCore::OrderKind::Attack, _ships, 0.0f, 0.0f, _enemies.front()});
    }
    else
    {
      order(_side, {GameCore::OrderKind::AttackMove, _ships, _enemyX, 0.0f});
    }
  };
  engage(1, west, east, static_cast<float>(_range) / 2.0f);
  engage(2, east, west, -static_cast<float>(_range) / 2.0f);

  Outcome outcome{0, 0.0, 0, 0};
  std::uint64_t tick = 1;
  std::uint32_t westLost = 0;
  std::uint32_t eastLost = 0;
  for (; tick <= LONGEST_TICKS && westLost < westCount && eastLost < eastCount; ++tick)
  {
    skirmish->Advance(tick);
    westLost = 0;
    eastLost = 0;
    for (const GameLogic::Skirmish::Loss& loss : skirmish->Losses())
    {
      (loss.entity <= westCount ? westLost : eastLost) += loss.entity <= westCount + eastCount ? 1u : 0u;
    }
  }
  outcome.seconds = static_cast<double>(tick - 1) / TICK_RATE;
  const bool westWins = eastLost == eastCount && westLost < westCount;
  const bool eastWins = westLost == westCount && eastLost < eastCount;
  const int westIs = _secondFirst ? 2 : 1;
  outcome.winner = westWins ? westIs : eastWins ? 3 - westIs : 0;
  for (const GameLogic::Skirmish::Loss& loss : skirmish->Losses())
  {
    (loss.kind == GameLogic::Skirmish::LossKind::Command ? outcome.commandLosses : outcome.reactorLosses)++;
  }
  return outcome;
}

} // namespace

TEST_CLASS(SmokeCheckTests)
{
public:
  // The plan's smoke check, not gate 5 (Design/MvpPlan.md, phase 5): its table, in the pull request's form, and its bar,
  // that no design wins more than 90 % of its duels against all the others.
  TEST_METHOD(RunsTheSmokeCheck)
  {
    const std::uint32_t seeds = SmokeSeeds();
    std::string table = std::format("Smoke check over {} seeds and both starts, {} s at most a duel\n\n", seeds, LONGEST_TICKS / TICK_RATE);
    table += "| Duel | From | First wins | Second wins | Neither | Mean length | Losses: command, reactor |\n";
    table += "|---|---|---|---|---|---|---|\n";
    std::map<std::string_view, std::pair<std::uint32_t, std::uint32_t>> record; // wins and duels, by design
    std::uint32_t losses = 0;
    for (const Pairing& pairing : PAIRINGS)
    {
      for (const std::int32_t range : RANGES)
      {
        std::uint32_t wins[3]{};
        double seconds = 0.0;
        std::uint32_t command = 0;
        std::uint32_t reactor = 0;
        for (std::uint32_t seed = 1; seed <= seeds; ++seed)
        {
          for (const bool secondFirst : {false, true})
          {
            const Outcome outcome = Fight(pairing, range, seed, secondFirst);
            ++wins[outcome.winner];
            seconds += outcome.seconds;
            command += outcome.commandLosses;
            reactor += outcome.reactorLosses;
          }
        }
        const std::uint32_t duels = 2 * seeds;
        record[pairing.first].first += wins[1];
        record[pairing.first].second += duels;
        record[pairing.second].first += wins[2];
        record[pairing.second].second += duels;
        losses += command + reactor;
        table += std::format("| {} × {} against {} × {} | {} | {} | {} | {} | {:.1f} s | {}, {} |\n", pairing.firstCount, pairing.first,
                             pairing.secondCount, pairing.second, range, wins[1], wins[2], wins[0], seconds / duels, command, reactor);
      }
    }
    table += "\n";
    for (const auto& [design, wins] : record)
    {
      table += std::format("{} wins {} of its {} duels, {:.0f} %\n", design, wins.first, wins.second, 100.0 * wins.first / wins.second);
    }
    Logger::WriteMessage(table.c_str());
    Assert::IsTrue(losses > 0, L"the duels are fought");
    // The bar holds the plan's 20 seeds to it; one seed, as CI runs, is too few to judge.
    if (seeds >= FULL_SEEDS)
    {
      for (const auto& [design, wins] : record)
      {
        Assert::IsTrue(10 * wins.first <= 9 * wins.second, std::format(L"no design wins more than 90 %: {}", Widen(design)).c_str());
      }
    }
  }

private:
  [[nodiscard]] static std::wstring Widen(std::string_view _text)
  {
    return {_text.begin(), _text.end()};
  }
};

} // namespace GameLogicTests
