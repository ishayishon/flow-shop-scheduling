#include <iostream>
#include <vector>
#include <algorithm>
#include <limits>
#include <random>
#include <cmath>
#include <string>
#include <fstream>
#include <stdexcept>
#include <unordered_set>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <sstream>
#include <cctype>

using namespace std;
namespace fs = std::filesystem;

// Flow shop with identical p on all machines + rejection budget.
// Objective: min Σ w_j C_j over scheduled jobs, s.t. Σ e_j (rejected) ≤ R.
// Rejecting all jobs is allowed if the rejection budget permits it.
// Scheduling subroutine for heuristics / WSPT-baseline: WSPT-MCI.

constexpr double EPS = 1e-9;

struct Job {
    int id;
    double processingTime; // p
    double weight;         // w
    double ratio;          // w/p
    double rejectWeight;   // e_j
};

struct RejectionResult {
    double cost;
    vector<int> scheduledOrder;
    vector<int> rejectedIds;
    double rejectWeightUsed;
    bool feasible;
};

static bool nearlyEqual(double a, double b) { return fabs(a - b) <= EPS; }
static bool definitelyLess(double a, double b) { return a < b - EPS; }
static bool definitelyGreater(double a, double b) { return a > b + EPS; }

static double clampNonNegSavings(double savings) {
    if (savings > EPS) return savings;
    return 0.0; // tiny negatives from FP noise → 0
}

static void validateJobs(const vector<Job>& jobs) {
    unordered_set<int> seen;
    for (const auto& j : jobs) {
        if (!seen.insert(j.id).second)
            throw invalid_argument("Duplicate job id: " + to_string(j.id));
        if (!(j.processingTime > 0))
            throw invalid_argument("Non-positive processingTime for id " + to_string(j.id));
        if (!(j.weight > 0))
            throw invalid_argument("Non-positive weight for id " + to_string(j.id));
        if (j.rejectWeight < -EPS)
            throw invalid_argument("Negative rejectWeight for id " + to_string(j.id));
    }
}

// ---------- MCI objective: C = sumP + (M-1)*maxP ----------
double calculateMCI_Cost(const vector<Job>& schedule, int m) {
    if (schedule.empty()) return 0.0;
    double total = 0, sumP = 0, maxP = 0;
    for (const auto& j : schedule) {
        sumP += j.processingTime;
        maxP = max(maxP, j.processingTime);
        total += j.weight * (sumP + (m - 1) * maxP);
    }
    return total;
}

// ---------- WSPT-MCI ----------
// Tie-breaks: higher w/p first; on equal ratio → smaller id.
// Insertion: on equal score → latest position.
// Each insertion evaluates all positions in O(n), so WSPT-MCI is O(n^2).
vector<Job> solveWSPT_MCI(vector<Job> jobs, int m) {
    int n = (int)jobs.size();
    if (n == 0) return {};

    sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) {
        if (definitelyGreater(a.ratio, b.ratio)) return true;
        if (definitelyLess(a.ratio, b.ratio)) return false;
        return a.id < b.id;
    });

    vector<Job> currentSchedule;
    for (int k = 0; k < n; ++k) {
        Job jNew = jobs[k];
        if (currentSchedule.empty()) {
            currentSchedule.push_back(jNew);
            continue;
        }

        int sz = (int)currentSchedule.size();
        vector<double> prefSumP(sz), prefMaxP(sz), prefObj(sz + 1, 0.0);
        vector<double> prefWeight(sz + 1, 0.0);
        vector<double> prefWeightSumP(sz + 1, 0.0);
        vector<double> prefWeightMaxP(sz + 1, 0.0);

        double sP = 0, mP = 0;
        for (int i = 0; i < sz; ++i) {
            sP += currentSchedule[i].processingTime;
            mP = max(mP, currentSchedule[i].processingTime);
            prefSumP[i] = sP;
            prefMaxP[i] = mP;
            double completion = sP + (m - 1) * mP;

            prefObj[i + 1] = prefObj[i] + currentSchedule[i].weight * completion;
            prefWeight[i + 1] = prefWeight[i] + currentSchedule[i].weight;
            prefWeightSumP[i + 1] = prefWeightSumP[i] + currentSchedule[i].weight * sP;
            prefWeightMaxP[i + 1] = prefWeightMaxP[i] + currentSchedule[i].weight * mP;
        }

        int firstMaxAtLeast = sz;
        int lo = 0, hi = sz;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (prefMaxP[mid] + EPS >= jNew.processingTime) hi = mid;
            else lo = mid + 1;
        }
        firstMaxAtLeast = lo;

        int bestPos = -1;
        double minScore = 0;

        for (int i = 0; i <= sz; ++i) {
            double pBefore = (i == 0) ? 0 : prefSumP[i - 1];
            double mBefore = (i == 0) ? 0 : prefMaxP[i - 1];

            double beforeCost = prefObj[i];
            double myCompletion = pBefore + jNew.processingTime
                                  + (m - 1) * max(mBefore, jNew.processingTime);
            double myCost = jNew.weight * myCompletion;

            double suffixWeight = prefWeight[sz] - prefWeight[i];
            double suffixWeightedSumP = prefWeightSumP[sz] - prefWeightSumP[i];

            int split = max(i, firstMaxAtLeast);
            double raisedWeight = prefWeight[split] - prefWeight[i];
            double suffixWeightedMax =
                raisedWeight * jNew.processingTime
                + (prefWeightMaxP[sz] - prefWeightMaxP[split]);

            double afterCost =
                suffixWeightedSumP
                + jNew.processingTime * suffixWeight
                + (m - 1) * suffixWeightedMax;

            double estimatedTotal = beforeCost + myCost + afterCost;
            if (bestPos == -1
                || definitelyLess(estimatedTotal, minScore)
                || (nearlyEqual(estimatedTotal, minScore) && i > bestPos)) {
                minScore = estimatedTotal;
                bestPos = i;
            }
        }
        currentSchedule.insert(currentSchedule.begin() + bestPos, jNew);
    }
    return currentSchedule;
}

static double scheduleCost(const vector<Job>& S, int M) {
    if (S.empty()) return 0.0;
    return calculateMCI_Cost(solveWSPT_MCI(S, M), M);
}

static RejectionResult makeResultFromSets(const vector<Job>& scheduled, const vector<Job>& rejected,
                                          double rejectUsed, int m) {
    RejectionResult res;
    res.rejectWeightUsed = rejectUsed;
    res.rejectedIds.clear();
    for (const auto& j : rejected) res.rejectedIds.push_back(j.id);
    sort(res.rejectedIds.begin(), res.rejectedIds.end());

    // Empty schedule is feasible: no scheduled jobs means objective 0.
    if (scheduled.empty()) {
        res.feasible = true;
        res.cost = 0.0;
        res.scheduledOrder.clear();
        return res;
    }

    res.feasible = true;
    vector<Job> order = solveWSPT_MCI(scheduled, m);
    res.cost = calculateMCI_Cost(order, m);
    res.scheduledOrder.clear();
    for (const auto& j : order) res.scheduledOrder.push_back(j.id);
    return res;
}

static bool betterRejectCandidate(double score, double benefit, double e, int id,
                                  double bestScore, double bestBenefit, double bestE, int bestId,
                                  bool hasBest) {
    if (!hasBest) return true;
    if (definitelyGreater(score, bestScore)) return true;
    if (definitelyLess(score, bestScore)) return false;
    if (definitelyGreater(benefit, bestBenefit)) return true;
    if (definitelyLess(benefit, bestBenefit)) return false;
    if (definitelyLess(e, bestE)) return true;
    if (definitelyGreater(e, bestE)) return false;
    return id < bestId;
}

static vector<int> jobIdsInOrder(const vector<Job>& jobs) {
    vector<int> ids;
    ids.reserve(jobs.size());
    for (const auto& j : jobs) ids.push_back(j.id);
    return ids;
}

static bool betterLocalSwap(double newCost, double newRejectUsed, const vector<int>& newRejIds,
                            const vector<int>& newSeqIds,
                            double curCost, double curRejectUsed, const vector<int>& curRejIds,
                            const vector<int>& curSeqIds) {
    if (definitelyLess(newCost, curCost)) return true;
    if (!nearlyEqual(newCost, curCost)) return false;
    if (definitelyLess(newRejectUsed, curRejectUsed)) return true;
    if (!nearlyEqual(newRejectUsed, curRejectUsed)) return false;
    if (newRejIds < curRejIds) return true;
    if (curRejIds < newRejIds) return false;
    return newSeqIds < curSeqIds;
}

// ---------- Heuristic 3: iterative greedy + one-shot best local swap, O(n^4) ----------
RejectionResult solveGreedyRejectionHeuristic(const vector<Job>& jobs, int M, double maxRejectWeight) {
    if (jobs.empty()) return makeResultFromSets({}, {}, 0.0, M);

    vector<Job> scheduled = jobs;
    vector<Job> rejected;
    double rejectUsed = 0.0;
    double currentCost = scheduleCost(scheduled, M);

    while (!scheduled.empty()) {
        int bestIdx = -1;
        double bestScore = 0, bestBenefit = 0, bestE = 0;
        double bestNewCost = currentCost;
        int bestId = -1;

        for (int i = 0; i < (int)scheduled.size(); ++i) {
            double e = scheduled[i].rejectWeight;
            if (definitelyGreater(rejectUsed + e, maxRejectWeight)) continue;

            vector<Job> trial;
            trial.reserve(scheduled.size() - 1);
            for (int k = 0; k < (int)scheduled.size(); ++k)
                if (k != i) trial.push_back(scheduled[k]);

            double newCost = scheduleCost(trial, M);
            double savings = clampNonNegSavings(currentCost - newCost);
            if (!(savings > EPS)) continue;

            double score = (e <= EPS) ? numeric_limits<double>::infinity() : (savings / e);
            if (betterRejectCandidate(score, savings, e, scheduled[i].id,
                                      bestScore, bestBenefit, bestE, bestId, bestIdx >= 0)) {
                bestIdx = i;
                bestScore = score;
                bestBenefit = savings;
                bestE = e;
                bestId = scheduled[i].id;
                bestNewCost = newCost;
            }
        }

        if (bestIdx < 0) break;

        rejected.push_back(scheduled[bestIdx]);
        scheduled.erase(scheduled.begin() + bestIdx);
        rejectUsed += bestE;
        currentCost = bestNewCost;
    }

    // One full scan: choose the single best 1-for-1 swap (keeps O(n^4))
    vector<int> curRejIds;
    for (const auto& j : rejected) curRejIds.push_back(j.id);
    sort(curRejIds.begin(), curRejIds.end());
    vector<int> curSeqIds = jobIdsInOrder(solveWSPT_MCI(scheduled, M));

    bool foundBestSwap = false;
    double bestSwapCost = currentCost;
    double bestSwapReject = rejectUsed;
    vector<int> bestSwapRejIds = curRejIds;
    vector<int> bestSwapSeqIds = curSeqIds;
    vector<Job> bestSched, bestRej;

    for (int ri = 0; ri < (int)rejected.size(); ++ri) {
        for (int si = 0; si < (int)scheduled.size(); ++si) {
            double newRejectUsed = rejectUsed - rejected[ri].rejectWeight + scheduled[si].rejectWeight;
            if (definitelyGreater(newRejectUsed, maxRejectWeight)) continue;

            vector<Job> newScheduled = scheduled;
            vector<Job> newRejected = rejected;
            swap(newScheduled[si], newRejected[ri]);

            double newCost = scheduleCost(newScheduled, M);
            vector<int> newRejIds;
            for (const auto& j : newRejected) newRejIds.push_back(j.id);
            sort(newRejIds.begin(), newRejIds.end());
            vector<int> newSeqIds = jobIdsInOrder(solveWSPT_MCI(newScheduled, M));

            if (betterLocalSwap(newCost, newRejectUsed, newRejIds, newSeqIds,
                                bestSwapCost, bestSwapReject, bestSwapRejIds, bestSwapSeqIds)) {
                foundBestSwap = true;
                bestSwapCost = newCost;
                bestSwapReject = newRejectUsed;
                bestSwapRejIds = newRejIds;
                bestSwapSeqIds = newSeqIds;
                bestSched = newScheduled;
                bestRej = newRejected;
            }
        }
    }

    if (foundBestSwap) {
        scheduled = std::move(bestSched);
        rejected = std::move(bestRej);
        rejectUsed = bestSwapReject;
        currentCost = bestSwapCost;
    }

    return makeResultFromSets(scheduled, rejected, rejectUsed, M);
}

// ---------- Heuristic 1: contribution / e from initial WSPT, ~O(n^2) if WSPT is O(n^2) ----------
RejectionResult solveContributionRatioHeuristic(const vector<Job>& jobs, int M, double maxRejectWeight) {
    if (jobs.empty()) return makeResultFromSets({}, {}, 0.0, M);

    vector<Job> initial = solveWSPT_MCI(jobs, M);

    struct Cand {
        int id;
        double rejectWeight;
        double benefit; // w*C
        double score;
    };
    vector<Cand> cands;
    cands.reserve(initial.size());

    double sumP = 0, maxP = 0;
    for (const auto& j : initial) {
        sumP += j.processingTime;
        maxP = max(maxP, j.processingTime);
        double Cj = sumP + (M - 1) * maxP;
        double contrib = j.weight * Cj;
        double e = j.rejectWeight;
        double score = (e <= EPS) ? numeric_limits<double>::infinity() : (contrib / e);
        cands.push_back({j.id, e, contrib, score});
    }

    sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        return betterRejectCandidate(a.score, a.benefit, a.rejectWeight, a.id,
                                     b.score, b.benefit, b.rejectWeight, b.id, true);
    });

    unordered_set<int> rejectSet;
    double rejectUsed = 0.0;
    for (const auto& c : cands) {
        if (definitelyGreater(rejectUsed + c.rejectWeight, maxRejectWeight)) continue;
        rejectSet.insert(c.id);
        rejectUsed += c.rejectWeight;
    }

    vector<Job> scheduled, rejected;
    for (const auto& j : jobs) {
        if (rejectSet.count(j.id)) rejected.push_back(j);
        else scheduled.push_back(j);
    }
    return makeResultFromSets(scheduled, rejected, rejectUsed, M);
}

// ---------- Heuristic 2: one-shot true savings / e, O(n * T_wspt) ----------
RejectionResult solveOneShotSavingsHeuristic(const vector<Job>& jobs, int M, double maxRejectWeight) {
    if (jobs.empty()) return makeResultFromSets({}, {}, 0.0, M);

    const double baseCost = scheduleCost(jobs, M);

    struct Cand {
        int id;
        double rejectWeight;
        double benefit; // savings
        double score;
    };
    vector<Cand> cands;
    cands.reserve(jobs.size());

    for (int i = 0; i < (int)jobs.size(); ++i) {
        vector<Job> trial;
        trial.reserve(jobs.size() - 1);
        for (int k = 0; k < (int)jobs.size(); ++k)
            if (k != i) trial.push_back(jobs[k]);

        double newCost = scheduleCost(trial, M);
        double savings = clampNonNegSavings(baseCost - newCost);
        double e = jobs[i].rejectWeight;
        double score = (e <= EPS) ? numeric_limits<double>::infinity() : (savings / e);
        cands.push_back({jobs[i].id, e, savings, score});
    }

    sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        return betterRejectCandidate(a.score, a.benefit, a.rejectWeight, a.id,
                                     b.score, b.benefit, b.rejectWeight, b.id, true);
    });

    unordered_set<int> rejectSet;
    double rejectUsed = 0.0;
    for (const auto& c : cands) {
        if (!(c.benefit > EPS)) continue;
        if (definitelyGreater(rejectUsed + c.rejectWeight, maxRejectWeight)) continue;
        rejectSet.insert(c.id);
        rejectUsed += c.rejectWeight;
    }

    vector<Job> scheduled, rejected;
    for (const auto& j : jobs) {
        if (rejectSet.count(j.id)) rejected.push_back(j);
        else scheduled.push_back(j);
    }
    return makeResultFromSets(scheduled, rejected, rejectUsed, M);
}

// ---------- Baseline matching the heuristics' model: all rejection sets + WSPT-MCI ----------
// (Not optimal order DP — fair comparison when every method schedules via WSPT-MCI.)
RejectionResult solveExactWithRejection(const vector<Job>& jobs, int M, double maxRejectWeight) {
    if (jobs.empty()) return makeResultFromSets({}, {}, 0.0, M);

    const int n = (int)jobs.size();
    const int Nmask = 1 << n;

    RejectionResult best;
    best.feasible = false;
    best.cost = numeric_limits<double>::infinity();
    best.rejectWeightUsed = -1;

    for (int rejMask = 0; rejMask < Nmask; ++rejMask) {
        double rejSum = 0.0;
        vector<Job> scheduled, rejected;
        for (int j = 0; j < n; ++j) {
            if (rejMask & (1 << j)) {
                rejSum += jobs[j].rejectWeight;
                rejected.push_back(jobs[j]);
            } else {
                scheduled.push_back(jobs[j]);
            }
        }
        if (definitelyGreater(rejSum, maxRejectWeight)) continue;

        RejectionResult cand = makeResultFromSets(scheduled, rejected, rejSum, M);
        if (!best.feasible
            || definitelyLess(cand.cost, best.cost)
            || (nearlyEqual(cand.cost, best.cost)
                && betterLocalSwap(cand.cost, cand.rejectWeightUsed, cand.rejectedIds, cand.scheduledOrder,
                                   best.cost, best.rejectWeightUsed, best.rejectedIds, best.scheduledOrder))) {
            best = cand;
        }
    }
    return best;
}

// Optional: true optimal order via DP over subsets (stronger than WSPT baseline).
RejectionResult solveExactOrderDP(const vector<Job>& jobs, int M, double maxRejectWeight) {
    if (jobs.empty()) return makeResultFromSets({}, {}, 0.0, M);

    const int n = (int)jobs.size();
    const int Nmask = 1 << n;
    const double INF = numeric_limits<double>::infinity();

    vector<double> sumP(Nmask, 0.0), maxP(Nmask, 0.0);
    vector<double> rejectOf(Nmask, 0.0);

    auto lowestBitIndex = [](int S) {
        int j = 0;
        while (((S >> j) & 1) == 0) ++j;
        return j;
    };

    for (int S = 1; S < Nmask; ++S) {
        int j = lowestBitIndex(S);
        int prev = S ^ (1 << j);
        sumP[S] = sumP[prev] + jobs[j].processingTime;
        maxP[S] = max(maxP[prev], jobs[j].processingTime);
        rejectOf[S] = rejectOf[prev] + jobs[j].rejectWeight;
    }

    vector<double> dp(Nmask, INF);
    vector<int> parent(Nmask, -1);
    dp[0] = 0.0;

    for (int S = 0; S < Nmask; ++S) {
        if (dp[S] >= INF / 2) continue;
        for (int j = 0; j < n; ++j) {
            if (S & (1 << j)) continue;
            int S2 = S | (1 << j);
            double Cj = sumP[S2] + (double)(M - 1) * maxP[S2];
            double cand = dp[S] + jobs[j].weight * Cj;
            if (definitelyLess(cand, dp[S2])) {
                dp[S2] = cand;
                parent[S2] = j;
            }
        }
    }

    const int full = Nmask - 1;
    double bestCost = INF;
    int bestS = -1;
    double bestRej = 0;

    // S = scheduled set; S=0 is allowed when the budget permits rejecting all jobs.
    for (int S = 0; S < Nmask; ++S) {
        int rejectedMask = full ^ S;
        if (definitelyGreater(rejectOf[rejectedMask], maxRejectWeight)) continue;
        if (definitelyLess(dp[S], bestCost)
            || (nearlyEqual(dp[S], bestCost) && rejectOf[rejectedMask] < bestRej - EPS)
            || bestS < 0) {
            bestCost = dp[S];
            bestS = S;
            bestRej = rejectOf[rejectedMask];
        }
    }

    RejectionResult res;
    res.feasible = (bestS >= 0);
    if (!res.feasible) {
        res.cost = INF;
        res.rejectWeightUsed = -1;
        return res;
    }

    res.cost = bestCost;
    res.rejectWeightUsed = bestRej;

    vector<int> order;
    int S = bestS;
    while (S) {
        int j = parent[S];
        order.push_back(jobs[j].id);
        S ^= (1 << j);
    }
    reverse(order.begin(), order.end());
    res.scheduledOrder = order;

    int rejectedMask = full ^ bestS;
    for (int j = 0; j < n; ++j)
        if (rejectedMask & (1 << j))
            res.rejectedIds.push_back(jobs[j].id);
    sort(res.rejectedIds.begin(), res.rejectedIds.end());
    return res;
}

static double totalRejectWeight(const vector<Job>& jobs) {
    double total = 0.0;
    for (const auto& j : jobs) total += j.rejectWeight;
    return total;
}

static fs::path createNextRunDirectory(const fs::path& runsDir) {
    fs::create_directories(runsDir);

    int bestRun = 0;
    const string prefix = "run_";
    for (const auto& entry : fs::directory_iterator(runsDir)) {
        if (!entry.is_directory()) continue;
        string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) != 0) continue;

        string numberPart = name.substr(prefix.size());
        if (numberPart.empty()) continue;
        bool digitsOnly = all_of(numberPart.begin(), numberPart.end(), [](unsigned char c) {
            return isdigit(c);
        });
        if (!digitsOnly) continue;
        bestRun = max(bestRun, stoi(numberPart));
    }

    ostringstream dirName;
    dirName << prefix << setw(3) << setfill('0') << (bestRun + 1);
    fs::path runDir = runsDir / dirName.str();
    fs::create_directories(runDir);
    return runDir;
}

static double chooseBudgetFromTotalReject(double totalReject, mt19937& gen) {
    // Mix tight, medium, and generous budgets.
    static const vector<double> ratios = {0.10, 0.20, 0.35, 0.50, 0.65, 0.80, 0.95};
    uniform_int_distribution<int> ratioIndexDist(0, (int)ratios.size() - 1);
    double budget = floor(totalReject * ratios[ratioIndexDist(gen)]);
    if (budget < 0.0) budget = 0.0;
    return budget;
}

static void generateVariedRandomJobs(vector<Job>& jobs, int numJobs, int pLimit, int wLimit,
                                     int rejectLimit, mt19937& gen, bool allowZeroReject = true) {
    uniform_int_distribution<int> pDist(1, pLimit);
    uniform_int_distribution<int> wDist(1, wLimit);
    uniform_int_distribution<int> rejectDist(allowZeroReject ? 0 : 1, rejectLimit);

    jobs.clear();
    jobs.reserve(numJobs);
    for (int id = 1; id <= numJobs; ++id) {
        double p = (double)pDist(gen);
        double w = (double)wDist(gen);
        double e = (double)rejectDist(gen);
        jobs.push_back({id, p, w, w / p, e});
    }
    validateJobs(jobs);
}

struct HeuristicStats {
    string name;
    int matched = 0;
    int matchedWithin5Pct = 0;
    int compared = 0;
    double sumAbsGap = 0.0;
    double sumRelGap = 0.0;
    double sumRelGapWith5PctTolerance = 0.0;
    double maxAbsGap = 0.0;
    double maxRelGap = 0.0;
    double maxRelGapWith5PctTolerance = 0.0;
    int relGapCount = 0;

    void add(double heuristicCost, double exactCost) {
        ++compared;
        const double absGap = heuristicCost - exactCost;
        sumAbsGap += absGap;
        if (absGap > maxAbsGap) maxAbsGap = absGap;
        if (exactCost > EPS) {
            const double relGap = absGap / exactCost;
            const double toleratedRelGap = (relGap <= 0.05 + EPS) ? 0.0 : relGap;
            sumRelGap += relGap;
            sumRelGapWith5PctTolerance += toleratedRelGap;
            ++relGapCount;
            if (relGap > maxRelGap) maxRelGap = relGap;
            if (toleratedRelGap > maxRelGapWith5PctTolerance) {
                maxRelGapWith5PctTolerance = toleratedRelGap;
            }
            if (relGap <= 0.05 + EPS) ++matchedWithin5Pct;
        } else if (fabs(absGap) <= EPS) {
            ++matchedWithin5Pct;
        }
        if (fabs(absGap) <= EPS) ++matched;
    }

    double accuracyPct() const {
        return compared ? (100.0 * matched / compared) : 0.0;
    }
    double avgAbs() const { return compared ? (sumAbsGap / compared) : 0.0; }
    double avgRelPct() const {
        return relGapCount ? (100.0 * sumRelGap / relGapCount) : 0.0;
    }
    double avgRelWith5PctTolerancePct() const {
        return relGapCount ? (100.0 * sumRelGapWith5PctTolerance / relGapCount) : 0.0;
    }
    double maxRelPct() const { return 100.0 * maxRelGap; }
    double maxRelWith5PctTolerancePct() const {
        return 100.0 * maxRelGapWith5PctTolerance;
    }

    void print() const {
        cout << "\n=== " << name << " vs WSPT-rejection baseline ===\n";
        if (compared == 0) {
            cout << "No compared instances.\n";
            return;
        }
        cout << "Matched baseline: " << matched << "/" << compared
             << " (" << accuracyPct() << "%)\n";
        cout << "Matched baseline within 5%: " << matchedWithin5Pct << "/" << compared
             << " (" << (100.0 * matchedWithin5Pct / compared) << "%)\n";
        cout << "Cost gap (heuristic - baseline):\n";
        cout << "  avg absolute: " << avgAbs() << "\n";
        cout << "  max absolute: " << maxAbsGap << "\n";
        if (relGapCount > 0) {
            cout << "  avg relative: " << avgRelPct() << "%\n";
            cout << "  max relative: " << maxRelPct() << "%\n";
            cout << "  avg relative with 5% tolerance: "
                 << avgRelWith5PctTolerancePct() << "%\n";
            cout << "  max relative with 5% tolerance: "
                 << maxRelWith5PctTolerancePct() << "%\n";
        }
    }
};

static void printH3BaselineFailureSummary(const HeuristicStats& greedyStats) {
    if (greedyStats.compared == 0) return;
    int anyError = greedyStats.compared - greedyStats.matched;
    int errorAbove5Pct = greedyStats.compared - greedyStats.matchedWithin5Pct;
    double anyErrorPct = 100.0 * anyError / greedyStats.compared;
    double errorAbove5PctPct = 100.0 * errorAbove5Pct / greedyStats.compared;
    cout << "\nH3 failure summary vs baseline:\n";
    cout << "  H3 was not equal to baseline: " << anyError << "/"
         << greedyStats.compared << " (" << anyErrorPct << "%)\n";
    cout << "  H3 was worse than baseline by more than 5%: " << errorAbove5Pct
         << "/" << greedyStats.compared << " (" << errorAbove5PctPct << "%)\n";
}

struct QualityRunRecord {
    int trial;
    int n;
    int M;
    double budget;
    double totalReject;
    double exact;
    double contrib;
    double oneShot;
    double greedy;
};

struct QualityJobRecord {
    int trial;
    int jobId;
    double processingTime;
    double weight;
    double rejectWeight;
};

struct ScalabilityRunRecord {
    int trial;
    int n;
    int M;
    double budget;
    double totalReject;
    double contrib;
    double oneShot;
    double greedy;
    double contribMs;
    double oneShotMs;
    double greedyMs;
};

struct ScalabilitySummary {
    int n = 0;
    int trials = 0;
    int contribBest = 0;
    int oneShotBest = 0;
    int greedyBest = 0;
    int contribWithin5Pct = 0;
    int oneShotWithin5Pct = 0;
    int greedyWithin5Pct = 0;
    double sumContribCost = 0.0;
    double sumOneShotCost = 0.0;
    double sumGreedyCost = 0.0;
    double sumContribGapPct = 0.0;
    double sumOneShotGapPct = 0.0;
    double sumGreedyGapPct = 0.0;
    double sumContribGapPctWith5Tolerance = 0.0;
    double sumOneShotGapPctWith5Tolerance = 0.0;
    double sumGreedyGapPctWith5Tolerance = 0.0;
    double maxContribGapPct = 0.0;
    double maxOneShotGapPct = 0.0;
    double maxGreedyGapPct = 0.0;
    double maxContribGapPctWith5Tolerance = 0.0;
    double maxOneShotGapPctWith5Tolerance = 0.0;
    double maxGreedyGapPctWith5Tolerance = 0.0;
    double sumContribMs = 0.0;
    double sumOneShotMs = 0.0;
    double sumGreedyMs = 0.0;

    void add(const ScalabilityRunRecord& record) {
        n = record.n;
        ++trials;

        double bestCost = min(record.contrib, min(record.oneShot, record.greedy));
        if (nearlyEqual(record.contrib, bestCost)) ++contribBest;
        if (nearlyEqual(record.oneShot, bestCost)) ++oneShotBest;
        if (nearlyEqual(record.greedy, bestCost)) ++greedyBest;

        auto gapPct = [&](double value) {
            if (bestCost <= EPS) return value > EPS ? numeric_limits<double>::infinity() : 0.0;
            return (value - bestCost) / bestCost * 100.0;
        };
        double contribGap = gapPct(record.contrib);
        double oneShotGap = gapPct(record.oneShot);
        double greedyGap = gapPct(record.greedy);
        double toleratedContribGap = contribGap <= 5.0 + EPS ? 0.0 : contribGap;
        double toleratedOneShotGap = oneShotGap <= 5.0 + EPS ? 0.0 : oneShotGap;
        double toleratedGreedyGap = greedyGap <= 5.0 + EPS ? 0.0 : greedyGap;
        if (contribGap <= 5.0 + EPS) ++contribWithin5Pct;
        if (oneShotGap <= 5.0 + EPS) ++oneShotWithin5Pct;
        if (greedyGap <= 5.0 + EPS) ++greedyWithin5Pct;

        sumContribCost += record.contrib;
        sumOneShotCost += record.oneShot;
        sumGreedyCost += record.greedy;
        sumContribGapPct += contribGap;
        sumOneShotGapPct += oneShotGap;
        sumGreedyGapPct += greedyGap;
        sumContribGapPctWith5Tolerance += toleratedContribGap;
        sumOneShotGapPctWith5Tolerance += toleratedOneShotGap;
        sumGreedyGapPctWith5Tolerance += toleratedGreedyGap;
        maxContribGapPct = max(maxContribGapPct, contribGap);
        maxOneShotGapPct = max(maxOneShotGapPct, oneShotGap);
        maxGreedyGapPct = max(maxGreedyGapPct, greedyGap);
        maxContribGapPctWith5Tolerance = max(maxContribGapPctWith5Tolerance, toleratedContribGap);
        maxOneShotGapPctWith5Tolerance = max(maxOneShotGapPctWith5Tolerance, toleratedOneShotGap);
        maxGreedyGapPctWith5Tolerance = max(maxGreedyGapPctWith5Tolerance, toleratedGreedyGap);
        sumContribMs += record.contribMs;
        sumOneShotMs += record.oneShotMs;
        sumGreedyMs += record.greedyMs;
    }

    void print() const {
        if (trials == 0) return;
        cout << "  n=" << n << " | trials=" << trials << "\n";
        cout << "    avg objective: H1=" << (sumContribCost / trials)
             << ", H2=" << (sumOneShotCost / trials)
             << ", H3=" << (sumGreedyCost / trials) << "\n";
        cout << "    best observed counts: H1=" << contribBest
             << ", H2=" << oneShotBest
             << ", H3=" << greedyBest << "\n";
        cout << "    within 5% of best observed counts: H1=" << contribWithin5Pct
             << ", H2=" << oneShotWithin5Pct
             << ", H3=" << greedyWithin5Pct << "\n";
        cout << "    avg gap from best observed: H1=" << (sumContribGapPct / trials) << "%"
             << ", H2=" << (sumOneShotGapPct / trials) << "%"
             << ", H3=" << (sumGreedyGapPct / trials) << "%\n";
        cout << "    max gap from best observed: H1=" << maxContribGapPct << "%"
             << ", H2=" << maxOneShotGapPct << "%"
             << ", H3=" << maxGreedyGapPct << "%\n";
        cout << "    avg gap with 5% tolerance: H1="
             << (sumContribGapPctWith5Tolerance / trials) << "%"
             << ", H2=" << (sumOneShotGapPctWith5Tolerance / trials) << "%"
             << ", H3=" << (sumGreedyGapPctWith5Tolerance / trials) << "%\n";
        cout << "    max gap with 5% tolerance: H1=" << maxContribGapPctWith5Tolerance << "%"
             << ", H2=" << maxOneShotGapPctWith5Tolerance << "%"
             << ", H3=" << maxGreedyGapPctWith5Tolerance << "%\n";
        cout << "    avg runtime ms: H1=" << (sumContribMs / trials)
             << ", H2=" << (sumOneShotMs / trials)
             << ", H3=" << (sumGreedyMs / trials) << "\n";
    }
};

static void printScalabilitySummary(const vector<ScalabilitySummary>& summaries) {
    ios::fmtflags oldFlags = cout.flags();
    streamsize oldPrecision = cout.precision();
    cout << fixed << setprecision(2);

    int totalTrials = 0;
    int h1Best = 0, h2Best = 0, h3Best = 0;
    int h1Within5Pct = 0, h2Within5Pct = 0, h3Within5Pct = 0;
    double h1Cost = 0.0, h2Cost = 0.0, h3Cost = 0.0;
    double h1Gap = 0.0, h2Gap = 0.0, h3Gap = 0.0;
    double h1GapTol = 0.0, h2GapTol = 0.0, h3GapTol = 0.0;
    double h1MaxGap = 0.0, h2MaxGap = 0.0, h3MaxGap = 0.0;
    double h1Ms = 0.0, h2Ms = 0.0, h3Ms = 0.0;
    for (const auto& s : summaries) {
        if (s.trials == 0) continue;
        totalTrials += s.trials;
        h1Best += s.contribBest;
        h2Best += s.oneShotBest;
        h3Best += s.greedyBest;
        h1Within5Pct += s.contribWithin5Pct;
        h2Within5Pct += s.oneShotWithin5Pct;
        h3Within5Pct += s.greedyWithin5Pct;
        h1Cost += s.sumContribCost;
        h2Cost += s.sumOneShotCost;
        h3Cost += s.sumGreedyCost;
        h1Gap += s.sumContribGapPct;
        h2Gap += s.sumOneShotGapPct;
        h3Gap += s.sumGreedyGapPct;
        h1GapTol += s.sumContribGapPctWith5Tolerance;
        h2GapTol += s.sumOneShotGapPctWith5Tolerance;
        h3GapTol += s.sumGreedyGapPctWith5Tolerance;
        h1MaxGap = max(h1MaxGap, s.maxContribGapPct);
        h2MaxGap = max(h2MaxGap, s.maxOneShotGapPct);
        h3MaxGap = max(h3MaxGap, s.maxGreedyGapPct);
        h1Ms += s.sumContribMs;
        h2Ms += s.sumOneShotMs;
        h3Ms += s.sumGreedyMs;
    }

    if (totalTrials > 0) {
        auto pct = [&](int count) {
            return 100.0 * count / totalTrials;
        };

        cout << "\n=== Scalability summary vs best observed heuristic ===\n";
        cout << "Compared scalability trials: " << totalTrials << "\n";
        cout << "Best observed counts:\n";
        cout << "  H1: " << h1Best << "/" << totalTrials << " (" << pct(h1Best) << "%)\n";
        cout << "  H2: " << h2Best << "/" << totalTrials << " (" << pct(h2Best) << "%)\n";
        cout << "  H3: " << h3Best << "/" << totalTrials << " (" << pct(h3Best) << "%)\n";
        cout << "Within 5% of best observed:\n";
        cout << "  H1: " << h1Within5Pct << "/" << totalTrials << " (" << pct(h1Within5Pct) << "%)\n";
        cout << "  H2: " << h2Within5Pct << "/" << totalTrials << " (" << pct(h2Within5Pct) << "%)\n";
        cout << "  H3: " << h3Within5Pct << "/" << totalTrials << " (" << pct(h3Within5Pct) << "%)\n";
        cout << "Average objective:\n";
        cout << "  H1: " << (h1Cost / totalTrials) << "\n";
        cout << "  H2: " << (h2Cost / totalTrials) << "\n";
        cout << "  H3: " << (h3Cost / totalTrials) << "\n";
        cout << "Average gap from best observed:\n";
        cout << "  H1: " << (h1Gap / totalTrials) << "%\n";
        cout << "  H2: " << (h2Gap / totalTrials) << "%\n";
        cout << "  H3: " << (h3Gap / totalTrials) << "%\n";
        cout << "Average gap with 5% tolerance:\n";
        cout << "  H1: " << (h1GapTol / totalTrials) << "%\n";
        cout << "  H2: " << (h2GapTol / totalTrials) << "%\n";
        cout << "  H3: " << (h3GapTol / totalTrials) << "%\n";
        cout << "Maximum gap from best observed:\n";
        cout << "  H1: " << h1MaxGap << "%\n";
        cout << "  H2: " << h2MaxGap << "%\n";
        cout << "  H3: " << h3MaxGap << "%\n";
        cout << "Average runtime:\n";
        cout << "  H1: " << (h1Ms / totalTrials) << " ms\n";
        cout << "  H2: " << (h2Ms / totalTrials) << " ms\n";
        cout << "  H3: " << (h3Ms / totalTrials) << " ms\n";
    }

    cout.flags(oldFlags);
    cout.precision(oldPrecision);
}

static void printH3ScalabilityFailureSummary(const vector<ScalabilitySummary>& summaries) {
    int totalTrials = 0;
    int h3NotBest = 0;
    int h3WorseThan5Pct = 0;
    for (const auto& s : summaries) {
        totalTrials += s.trials;
        h3NotBest += s.trials - s.greedyBest;
        h3WorseThan5Pct += s.trials - s.greedyWithin5Pct;
    }

    if (totalTrials == 0) return;
    double h3NotBestPct = 100.0 * h3NotBest / totalTrials;
    double h3WorseThan5PctPct = 100.0 * h3WorseThan5Pct / totalTrials;
    cout << "\nH3 failure summary vs other heuristics:\n";
    cout << "  H3 was not the best observed heuristic: " << h3NotBest << "/"
         << totalTrials << " (" << h3NotBestPct << "%)\n";
    cout << "  H3 was worse than the best observed heuristic by more than 5%: "
         << h3WorseThan5Pct << "/" << totalTrials << " ("
         << h3WorseThan5PctPct << "%)\n";
}

static void writeBenchmarkCsv(const string& path, const vector<QualityRunRecord>& records) {
    ofstream out(path);
    out << "trial,n,M,budget,total_reject,exact,contrib_n2,oneshot_n3,greedy_n4\n";
    for (const auto& r : records) {
        out << r.trial << "," << r.n << "," << r.M << "," << r.budget << ","
            << r.totalReject << "," << r.exact << "," << r.contrib << ","
            << r.oneShot << "," << r.greedy << "\n";
    }
}

static void writeBenchmarkJobsCsv(const string& path, const vector<QualityJobRecord>& records) {
    ofstream out(path);
    out << "trial,job_id,p,w,rejectWeight\n";
    for (const auto& r : records) {
        out << r.trial << "," << r.jobId << "," << r.processingTime << ","
            << r.weight << "," << r.rejectWeight << "\n";
    }
}

static void writeScalabilityCsv(const string& path, const vector<ScalabilityRunRecord>& records) {
    ofstream out(path);
    out << "trial,n,M,budget,total_reject,contrib_n2,oneshot_n3,greedy_n4,"
        << "best_observed,contrib_gap_pct,oneshot_gap_pct,greedy_gap_pct,"
        << "contrib_gap_pct_with_5_tolerance,oneshot_gap_pct_with_5_tolerance,greedy_gap_pct_with_5_tolerance,"
        << "contrib_is_best,oneshot_is_best,greedy_is_best,"
        << "contrib_within_5_pct,oneshot_within_5_pct,greedy_within_5_pct,"
        << "contrib_ms,oneshot_ms,greedy_ms\n";
    for (const auto& r : records) {
        double bestCost = min(r.contrib, min(r.oneShot, r.greedy));
        auto gapPct = [&](double value) {
            if (bestCost <= EPS) return value > EPS ? numeric_limits<double>::infinity() : 0.0;
            return (value - bestCost) / bestCost * 100.0;
        };
        out << r.trial << "," << r.n << "," << r.M << "," << r.budget << ","
            << r.totalReject << "," << r.contrib << "," << r.oneShot << ","
            << r.greedy << "," << bestCost << ","
            << gapPct(r.contrib) << "," << gapPct(r.oneShot) << "," << gapPct(r.greedy) << ","
            << (gapPct(r.contrib) <= 5.0 + EPS ? 0.0 : gapPct(r.contrib)) << ","
            << (gapPct(r.oneShot) <= 5.0 + EPS ? 0.0 : gapPct(r.oneShot)) << ","
            << (gapPct(r.greedy) <= 5.0 + EPS ? 0.0 : gapPct(r.greedy)) << ","
            << (nearlyEqual(r.contrib, bestCost) ? 1 : 0) << ","
            << (nearlyEqual(r.oneShot, bestCost) ? 1 : 0) << ","
            << (nearlyEqual(r.greedy, bestCost) ? 1 : 0) << ","
            << (gapPct(r.contrib) <= 5.0 + EPS ? 1 : 0) << ","
            << (gapPct(r.oneShot) <= 5.0 + EPS ? 1 : 0) << ","
            << (gapPct(r.greedy) <= 5.0 + EPS ? 1 : 0) << ","
            << r.contribMs << "," << r.oneShotMs << "," << r.greedyMs << "\n";
    }
}

static void writeSummaryCsv(const string& path, const vector<HeuristicStats>& stats) {
    ofstream out(path);
    out << "heuristic,accuracy_pct,within_5_pct_accuracy,avg_abs_gap,max_abs_gap,"
        << "avg_rel_gap_pct,max_rel_gap_pct,avg_rel_gap_pct_with_5_tolerance,"
        << "max_rel_gap_pct_with_5_tolerance,matched,matched_within_5_pct,compared\n";
    for (const auto& s : stats) {
        out << "\"" << s.name << "\","
            << s.accuracyPct() << ","
            << (s.compared ? (100.0 * s.matchedWithin5Pct / s.compared) : 0.0) << ","
            << s.avgAbs() << ","
            << s.maxAbsGap << ","
            << s.avgRelPct() << ","
            << s.maxRelPct() << ","
            << s.avgRelWith5PctTolerancePct() << ","
            << s.maxRelWith5PctTolerancePct() << ","
            << s.matched << ","
            << s.matchedWithin5Pct << ","
            << s.compared << "\n";
    }
}

int main() {
    constexpr bool runBenchmark = true;
    constexpr int qualityTrials = 5000;
    constexpr int qualityMinJobs = 5;
    constexpr int qualityMaxJobs = 14;
    constexpr int minMachines = 2;
    constexpr int maxMachines = 10;
    constexpr int processingLimit = 100;
    constexpr int weightLimit = 100;
    constexpr int rejectLimit = 20;
    constexpr int scalabilityTrialsPerSize = 30;
    constexpr unsigned randomSeed = 0;
    constexpr bool allowZeroReject = true;

    if (runBenchmark) {
        unsigned seed = randomSeed;
        if (seed == 0) {
            random_device rd;
            seed = rd();
        }
        mt19937 gen(seed);

        uniform_int_distribution<int> qualityJobCountDist(qualityMinJobs, qualityMaxJobs);
        uniform_int_distribution<int> machineDist(minMachines, maxMachines);

        // Heuristic numbering aligned with partner notes:
        // 1 = contribution O(n^2), 2 = one-shot O(n^3), 3 = greedy+local O(n^4)
        HeuristicStats contribStats{"Contribution-ratio O(n^2) [H1]"};
        HeuristicStats oneShotStats{"One-shot savings O(n^3) [H2]"};
        HeuristicStats greedyStats{"Greedy + local O(n^4) [H3]"};
        int exactFeasible = 0;

        vector<QualityRunRecord> qualityRecords;
        qualityRecords.reserve(qualityTrials);
        vector<QualityJobRecord> qualityJobRecords;
        qualityJobRecords.reserve(qualityTrials * qualityMaxJobs);

        for (int t = 0; t < qualityTrials; ++t) {
            vector<Job> jobs;
            int n = qualityJobCountDist(gen);
            int M = machineDist(gen);
            generateVariedRandomJobs(jobs, n, processingLimit, weightLimit,
                                     rejectLimit, gen, allowZeroReject);
            for (const auto& job : jobs) {
                qualityJobRecords.push_back({
                    t + 1, job.id, job.processingTime, job.weight, job.rejectWeight
                });
            }
            double totalReject = totalRejectWeight(jobs);
            double maxRejectWeight = chooseBudgetFromTotalReject(totalReject, gen);

            // Baseline = all feasible rejection sets + WSPT-MCI (same model as heuristics)
            RejectionResult exact = solveExactWithRejection(jobs, M, maxRejectWeight);
            if (!exact.feasible) continue;
            ++exactFeasible;

            RejectionResult contrib = solveContributionRatioHeuristic(jobs, M, maxRejectWeight);
            RejectionResult oneShot = solveOneShotSavingsHeuristic(jobs, M, maxRejectWeight);
            RejectionResult greedy = solveGreedyRejectionHeuristic(jobs, M, maxRejectWeight);

            if (!greedy.feasible || !oneShot.feasible || !contrib.feasible) continue;

            contribStats.add(contrib.cost, exact.cost);
            oneShotStats.add(oneShot.cost, exact.cost);
            greedyStats.add(greedy.cost, exact.cost);

            qualityRecords.push_back({
                t + 1, n, M, maxRejectWeight, totalReject,
                exact.cost, contrib.cost, oneShot.cost, greedy.cost
            });
        }

        cout << "Quality benchmark trials: " << qualityTrials
             << " | baseline-feasible: " << exactFeasible << "\n";
        cout << "n is random in [" << qualityMinJobs << "," << qualityMaxJobs << "]"
             << " | M is random in [" << minMachines << "," << maxMachines << "]"
             << " | budget is a random ratio of total rejection cost.\n";
        cout << "Baseline = enumerate rejection sets + WSPT-MCI (not order-DP).\n";
        contribStats.print();
        oneShotStats.print();
        greedyStats.print();
        printH3BaselineFailureSummary(greedyStats);

        fs::path runDir = createNextRunDirectory("runs");
        cout << "\nSaving this run into: " << runDir.string() << "\n";

        writeBenchmarkCsv("benchmark_runs.csv", qualityRecords);
        writeBenchmarkJobsCsv("benchmark_trial_jobs.csv", qualityJobRecords);
        writeSummaryCsv("benchmark_summary.csv", {greedyStats, oneShotStats, contribStats});
        writeBenchmarkCsv((runDir / "benchmark_runs.csv").string(), qualityRecords);
        writeBenchmarkJobsCsv((runDir / "benchmark_trial_jobs.csv").string(), qualityJobRecords);
        writeSummaryCsv((runDir / "benchmark_summary.csv").string(), {greedyStats, oneShotStats, contribStats});

        vector<ScalabilityRunRecord> scalabilityRecords;
        vector<ScalabilitySummary> scalabilitySummaries;
        const vector<int> scalabilitySizes = {20, 50, 100, 150, 200};
        int scalabilityTrial = 1;

        cout << "\nScalability benchmark without exact baseline:\n";
        for (int n : scalabilitySizes) {
            ScalabilitySummary sizeSummary;
            for (int rep = 0; rep < scalabilityTrialsPerSize; ++rep) {
                vector<Job> jobs;
                int M = machineDist(gen);
                generateVariedRandomJobs(jobs, n, processingLimit, weightLimit,
                                         rejectLimit, gen, allowZeroReject);
                double totalReject = totalRejectWeight(jobs);
                double maxRejectWeight = chooseBudgetFromTotalReject(totalReject, gen);

                auto timeMs = [](auto&& fn, RejectionResult& out) {
                    auto start = chrono::high_resolution_clock::now();
                    out = fn();
                    auto finish = chrono::high_resolution_clock::now();
                    chrono::duration<double, milli> elapsed = finish - start;
                    return elapsed.count();
                };

                RejectionResult contrib, oneShot, greedy;
                double contribMs = timeMs([&]() {
                    return solveContributionRatioHeuristic(jobs, M, maxRejectWeight);
                }, contrib);
                double oneShotMs = timeMs([&]() {
                    return solveOneShotSavingsHeuristic(jobs, M, maxRejectWeight);
                }, oneShot);
                double greedyMs = timeMs([&]() {
                    return solveGreedyRejectionHeuristic(jobs, M, maxRejectWeight);
                }, greedy);

                ScalabilityRunRecord record{
                    scalabilityTrial++, n, M, maxRejectWeight, totalReject,
                    contrib.cost, oneShot.cost, greedy.cost,
                    contribMs, oneShotMs, greedyMs
                };
                scalabilityRecords.push_back(record);
                sizeSummary.add(record);
            }
            scalabilitySummaries.push_back(sizeSummary);
            cout << "  completed n=" << n << " (" << scalabilityTrialsPerSize << " trials)\n";
        }

        writeScalabilityCsv("scalability_runs.csv", scalabilityRecords);
        writeScalabilityCsv((runDir / "scalability_runs.csv").string(), scalabilityRecords);

        cout << "\nScalability summary without exact baseline:\n";
        cout << "These results compare the heuristics against each other only.\n";
        cout << "No exact baseline is computed here because exhaustive rejection enumeration is exponential.\n";
        printScalabilitySummary(scalabilitySummaries);
        printH3ScalabilityFailureSummary(scalabilitySummaries);

        cout << "\nWrote benchmark_runs.csv, benchmark_trial_jobs.csv, benchmark_summary.csv, and scalability_runs.csv\n";
        cout << "Also saved the full run under " << runDir.string() << "\n";
        return 0;
    }

    int m = 2;
    double R = 5;
    vector<Job> jobs = {
        {1, 1, 100, 100.0, 3},
        {2, 1, 2, 2.0, 2},
        {3, 1, 2, 2.0, 2},
        {4, 1, 2, 2.0, 1}
    };
    validateJobs(jobs);

    RejectionResult exact = solveExactWithRejection(jobs, m, R);
    RejectionResult greedy = solveGreedyRejectionHeuristic(jobs, m, R);
    RejectionResult oneShot = solveOneShotSavingsHeuristic(jobs, m, R);
    RejectionResult contrib = solveContributionRatioHeuristic(jobs, m, R);

    cout << "Max reject weight: " << R << "\n\n";
    cout << "WSPT-baseline cost:       " << exact.cost << "\n";
    cout << "Greedy O(n^4) cost:       " << greedy.cost << "\n";
    cout << "One-shot O(n^3) cost:     " << oneShot.cost << "\n";
    cout << "Contribution O(n^2) cost: " << contrib.cost << "\n";

    return 0;
}
