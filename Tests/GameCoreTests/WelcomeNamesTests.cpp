#include "pch.h"

#include "TestSupport.h"
#include "WelcomeNames.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{
namespace
{

using Bytes = std::vector<std::uint8_t>;

[[nodiscard]] GameCore::WelcomeNames SampleNames()
{
  return {{"Miner", "Gunship", "Lancer", "Cruiser", "StationCore", "AsteroidA"}, {"Blue", "Red"}};
}

void ExpectRefusal(GameCore::NamesError _expected, const Bytes& _bytes, const std::wstring& _case)
{
  const auto decoded = GameCore::DecodeWelcomeNames(_bytes);
  Assert::IsFalse(decoded.has_value(), (_case + L": accepted").c_str());
  Assert::AreEqual(std::string(GameCore::NamesErrorName(_expected)),
                   std::string(decoded ? "Accepted" : GameCore::NamesErrorName(decoded.error())), _case.c_str());
}

} // namespace

// Design/ADR/ADR-030: the game's names in the skirmish's welcome, which the engine carries unread.
TEST_CLASS(WelcomeNamesTests)
{
public:
  TEST_METHOD(RoundTrips)
  {
    const GameCore::WelcomeNames names = SampleNames();
    const Bytes bytes = GameCore::EncodeWelcomeNames(names);
    Assert::IsTrue(bytes.front() == GameCore::WELCOME_NAMES_VERSION, L"the version first");
    const auto decoded = GameCore::DecodeWelcomeNames(bytes);
    Assert::IsTrue(decoded.has_value(), L"decoded");
    const GameCore::WelcomeNames value = decoded.value_or(GameCore::WelcomeNames{});
    Assert::IsTrue(value.composites == names.composites, L"the composites' names, in order");
    Assert::IsTrue(value.sides == names.sides, L"the sides' names, in order");
    Assert::IsTrue(GameCore::EncodeWelcomeNames(value) == bytes, L"the same bytes again");

    // A world that sends no payload names nothing, and that is no refusal.
    const auto empty = GameCore::DecodeWelcomeNames({});
    Assert::IsTrue(empty.has_value() && empty->composites.empty() && empty->sides.empty(), L"no payload, no names");
  }

  // Every prefix of the bytes but the empty one is too short; and each other refusal by its name.
  TEST_METHOD(RefusesByName)
  {
    const Bytes bytes = GameCore::EncodeWelcomeNames(SampleNames());
    for (std::size_t length = 1; length < bytes.size(); ++length)
    {
      ExpectRefusal(GameCore::NamesError::Truncated, Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length)),
                    std::format(L"cut to {} of {} bytes", length, bytes.size()));
    }
    {
      Bytes version = bytes;
      version.front() = static_cast<std::uint8_t>(GameCore::WELCOME_NAMES_VERSION + 1);
      ExpectRefusal(GameCore::NamesError::UnsupportedVersion, version, L"another version");
    }
    {
      // The first name's first letter, after the version, the count and the length byte.
      Bytes badName = bytes;
      badName[6] = '_';
      ExpectRefusal(GameCore::NamesError::BadName, badName, L"a name with an underscore");
      badName[5] = 0;
      ExpectRefusal(GameCore::NamesError::BadName, badName, L"an empty name");
    }
    {
      Bytes trailing = bytes;
      trailing.push_back(0);
      ExpectRefusal(GameCore::NamesError::TrailingBytes, trailing, L"a byte after the last name");
    }
    {
      // A count no payload could hold is refused before anything is reserved for it.
      Bytes huge = bytes;
      for (std::size_t i = 1; i < 5; ++i)
      {
        huge[i] = 0xFF;
      }
      ExpectRefusal(GameCore::NamesError::Truncated, huge, L"a count beyond the bytes");
    }
  }

  TEST_METHOD(RefusesToEncodeANameTooLong)
  {
    GameCore::WelcomeNames names = SampleNames();
    names.sides.back() = std::string(255, 'a');
    Assert::IsTrue(GameCore::DecodeWelcomeNames(GameCore::EncodeWelcomeNames(names)).has_value(), L"255 letters");
    names.sides.back().push_back('a');
    bool refused = false;
    try
    {
      static_cast<void>(GameCore::EncodeWelcomeNames(names));
    }
    catch (const std::invalid_argument&)
    {
      refused = true;
    }
    Assert::IsTrue(refused, L"256 letters are not encoded");
  }
};

} // namespace GameCoreTests
