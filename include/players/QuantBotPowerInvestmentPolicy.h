#ifndef QUANTBOT_POWER_INVESTMENT_POLICY_H
#define QUANTBOT_POWER_INVESTMENT_POLICY_H

#include <data.h>
#include <Definitions.h>
#include <SDL_stdinc.h>
#include <algorithm>
#include <cstdint>

// ---------------------------------------------------------------------------
// Generation outside the city simulation
//
// Dynasty-aligned degradation charges every structure one hitpoint per fifteen
// game seconds while raw production is below raw demand -- in Vanilla too,
// where House::hasPower() answers true regardless. So a generator bought
// in Vanilla avoids recurring repair costs. Factories have no immediate
// power slowdown, although the radar display still requires full power.
//
// That makes it an investment, and this is the gate it has to pass. All of it
// is integer arithmetic over figures measured from the live game, so every
// lockstep peer and every reloaded save reaches the same verdict.
//
//   1. Size the whole package. Half a package closes nothing and therefore
//      saves nothing, so the deficit is divided by the capacity one purchase
//      actually adds and the capital of all of them is what gets judged.
//   2. It must be affordable out of cash that the core queues and the
//      operating reserve do not need.
//   3. The forecast recurring income must be able to replace that capital within
//      thirty game seconds.
//   4. Either the repairs it avoids repay that capital within ten game
//      minutes -- priced with the engine's own integer repair formula over
//      exactly the buildings the repair policy keeps repaired -- or the base is
//      prosperous enough that the spare cash left after the reserve no longer
//      needs to be argued for. Buildings above 512 maximum health repair for
//      free in that formula and correctly contribute nothing to the payback,
//      which on its own would keep a normal mature base unpowered forever.
//
// The caller then orders one generator and re-evaluates on the next pass.
// ---------------------------------------------------------------------------

namespace QuantBotPowerInvestmentPolicy {

// StructureBase::update() takes one hitpoint per fifteen game seconds.
constexpr int hitpointsLostPerMinute = 4;
// Used only when a mod publishes no usable output for its generator.
constexpr int defaultGeneratorOutput = 100;
constexpr int incomeReplacementSeconds = 30;
constexpr int repairPaybackMinutes = 10;
// Spare cash, after the operating reserve, above which a base no longer has to
// justify its own generation through avoided repairs. Same figure the repair
// policy already uses for discretionary repairs, so the two agree.
constexpr int prosperousSpareCash = 5000;

struct Investment {
    int deficit = 0;                 // raw demand less production, repairs and pending output
    int generatorOutput = 0;         // capacity one purchase adds
    int unitCapital = 0;             // its price including the full foundation
    int spendable = 0;               // cash the core queues and reserve do not need
    int netIncome = 0;               // forecast net receipts over incomeMinutes
    int incomeMinutes = 0;
    int repairMilliPerHitpoint = 0;  // summed over the buildings the policy maintains
};

inline int generatorsNeeded(int deficit, int generatorOutput) {
    if (deficit <= 0) return 0;
    const int output = generatorOutput > 0 ? generatorOutput : defaultGeneratorOutput;
    return (deficit + output - 1) / output;
}

inline int packageCapital(const Investment& state) {
    return generatorsNeeded(state.deficit, state.generatorOutput) * std::max(0, state.unitCapital);
}

inline bool incomeReplacesCapital(const Investment& state, int capital) {
    return capital > 0 && state.incomeMinutes > 0 && state.netIncome > 0
        && int64_t(state.netIncome) * incomeReplacementSeconds
            >= int64_t(capital) * state.incomeMinutes * 60;
}

// Credits of repair a closed deficit stops paying for, every game minute.
inline int avoidedRepairPerMinute(const Investment& state) {
    return int(int64_t(std::max(0, state.repairMilliPerHitpoint))
        * hitpointsLostPerMinute / 1000);
}

inline bool repaysThroughAvoidedRepairs(const Investment& state, int capital) {
    return capital > 0
        && int64_t(avoidedRepairPerMinute(state)) * repairPaybackMinutes >= capital;
}

inline bool prosperous(const Investment& state) {
    return state.spendable > prosperousSpareCash;
}

enum class Verdict { BuyForRepairs, BuyWhileProsperous, NoDeficit, Unaffordable,
                     IncomeTooLow, RepairsTooCheap };

inline bool buys(Verdict verdict) {
    return verdict == Verdict::BuyForRepairs || verdict == Verdict::BuyWhileProsperous;
}

inline Verdict evaluate(const Investment& state) {
    const int capital = packageCapital(state);
    if (capital <= 0) return Verdict::NoDeficit;
    if (state.spendable < capital) return Verdict::Unaffordable;
    if (!incomeReplacesCapital(state, capital)) return Verdict::IncomeTooLow;
    if (repaysThroughAvoidedRepairs(state, capital)) return Verdict::BuyForRepairs;
    if (prosperous(state)) return Verdict::BuyWhileProsperous;
    return Verdict::RepairsTooCheap;
}

inline const char* describe(Verdict verdict) {
    switch (verdict) {
        case Verdict::BuyForRepairs:      return "avoided_repairs_repay_generation";
        case Verdict::BuyWhileProsperous: return "prosperous_generation_investment";
        case Verdict::NoDeficit:          return "no_raw_power_deficit";
        case Verdict::Unaffordable:       return "package_beyond_spendable_cash";
        case Verdict::IncomeTooLow:       return "income_cannot_replace_capital";
        case Verdict::RepairsTooCheap:    return "avoided_repairs_do_not_repay";
    }
    return "unknown";
}

// Among the generators this builder can actually order, the cheapest way to
// close the whole deficit wins; equal packages keep the lowest item id so
// every peer chooses the same building.
struct GeneratorChoice {
    Uint32 item = NONE_ID;
    int output = 0;
    int unitCapital = 0;
    int packageCapital = 0;
};

inline bool preferGenerator(const GeneratorChoice& candidate, const GeneratorChoice& best) {
    if (best.item == NONE_ID) return candidate.item != NONE_ID;
    if (candidate.item == NONE_ID) return false;
    if (candidate.packageCapital != best.packageCapital)
        return candidate.packageCapital < best.packageCapital;
    return candidate.item < best.item;
}

} // namespace QuantBotPowerInvestmentPolicy
#endif
