// Catch2 tests for the D2R sunder-charm pair map (crafting-recipe knowledge
// used by the reconcile subcommand). Display names come from item-names.json
// at runtime, not from code.

#include "d2r/SunderCharms.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("sunderPairedId round-trips between Latent and Renewed", "[sunder]") {
    REQUIRE(d2r::sunderPairedId(426) == 427);
    REQUIRE(d2r::sunderPairedId(427) == 426);
    REQUIRE(d2r::sunderPairedId(428) == 433);
    REQUIRE(d2r::sunderPairedId(433) == 428);
    REQUIRE(d2r::sunderPairedId(429) == 434);
    REQUIRE(d2r::sunderPairedId(434) == 429);
    REQUIRE(d2r::sunderPairedId(430) == 435);
    REQUIRE(d2r::sunderPairedId(435) == 430);
    REQUIRE(d2r::sunderPairedId(431) == 436);
    REQUIRE(d2r::sunderPairedId(436) == 431);
    REQUIRE(d2r::sunderPairedId(432) == 437);
    REQUIRE(d2r::sunderPairedId(437) == 432);
    REQUIRE(d2r::sunderPairedId(0)   == 0);
    REQUIRE(d2r::sunderPairedId(345) == 0);
}
