// EasyLocal 3 port of the EasyLocal 4 exam-timetabling example
// (examples/exam_timetabling): move one exam to another timeslot; three soft
// components (student conflicts x1000, consecutive exams x10, timeslot load
// x1). In delta mode mixed (the default, the example's configuration),
// conflicts and consecutive exams have the deltas of cost_components.hpp and
// cost_deltas.hpp, and the timeslot load has none and is evaluated on a copy
// of the solution with the move applied; delta mode all adds the timeslot-load
// delta of common/exam_timetabling_deltas.hpp, delta mode none drops them all.
#pragma once

#include "helpers/solutionmanager.hh"
#include "helpers/costcomponent.hh"
#include "helpers/deltacostcomponent.hh"
#include "helpers/neighborhoodexplorer.hh"
#include "utils/random.hh"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace exam
{

using namespace EasyLocal::Core;

using CFtype = long;
using CostStructure = DefaultCostStructure<CFtype>;

struct Conflict
{
    std::size_t first, second;
    long students;
};

struct ConflictingExam
{
    std::size_t exam;
    long students;
};

// Same file format as ExamTimetablingInstance::read
// (examples/exam_timetabling/instance.hpp): exams,
// timeslots, conflicts, then one "first second students" line per conflict.
class Input
{
public:
    explicit Input(const std::string& path)
    {
        std::ifstream is(path);
        if (!is)
            throw std::runtime_error("cannot open exam-timetabling instance: " + path);
        std::size_t conflict_count = 0;
        if (!(is >> exam_count >> timeslot_count >> conflict_count))
            throw std::runtime_error("invalid exam-timetabling instance header: " + path);
        conflicts.reserve(conflict_count);
        for (std::size_t i = 0; i < conflict_count; ++i)
        {
            Conflict c{};
            if (!(is >> c.first >> c.second >> c.students))
                throw std::runtime_error("invalid exam-timetabling conflict data: " + path);
            if (c.first >= exam_count || c.second >= exam_count || c.first == c.second || c.students < 0)
                throw std::runtime_error("invalid exam-timetabling instance: " + path);
            conflicts.push_back(c);
        }
        if (timeslot_count == 0 && (exam_count != 0 || !conflicts.empty()))
            throw std::runtime_error("invalid exam-timetabling instance: " + path);
        conflicts_by_exam.resize(exam_count);
        for (const auto& c : conflicts)
        {
            conflicts_by_exam[c.first].push_back({c.second, c.students});
            conflicts_by_exam[c.second].push_back({c.first, c.students});
        }
    }

    std::size_t exam_count = 0, timeslot_count = 0;
    std::vector<Conflict> conflicts;
    // For each exam, the exams it shares students with, as conflicts_by_exam()
    // of examples/exam_timetabling/instance.hpp: the deltas visit only these.
    std::vector<std::vector<ConflictingExam>> conflicts_by_exam;
};

class Timetable
{
public:
    explicit Timetable(const Input& in) : timeslot_by_exam(in.exam_count) {}
    std::vector<std::size_t> timeslot_by_exam;
};

inline std::ostream& operator<<(std::ostream& os, const Timetable& st)
{
    for (std::size_t i = 0; i < st.timeslot_by_exam.size(); ++i)
        os << (i ? " " : "") << st.timeslot_by_exam[i];
    return os;
}

class MoveExam
{
public:
    std::size_t exam = 0, destination = 0;
};

inline bool operator==(const MoveExam& a, const MoveExam& b)
{
    return a.exam == b.exam && a.destination == b.destination;
}
inline bool operator!=(const MoveExam& a, const MoveExam& b) { return !(a == b); }
inline bool operator<(const MoveExam& a, const MoveExam& b)
{
    return a.exam < b.exam || (a.exam == b.exam && a.destination < b.destination);
}
inline std::ostream& operator<<(std::ostream& os, const MoveExam& mv)
{
    return os << "exam " << mv.exam << " -> timeslot " << mv.destination;
}

class ExamSolutionManager : public SolutionManager<Input, Timetable, CostStructure>
{
public:
    explicit ExamSolutionManager(const Input& in)
        : SolutionManager<Input, Timetable, CostStructure>(in, "ExamSolutionManager") {}

    void RandomState(Timetable& st) override
    {
        st.timeslot_by_exam.resize(in.exam_count);
        for (auto& t : st.timeslot_by_exam)
            t = Random::Uniform<std::size_t>(0, in.timeslot_count - 1);
    }

    bool CheckConsistency(const Timetable& st) const override
    {
        return st.timeslot_by_exam.size() == in.exam_count;
    }
};

class StudentConflicts : public CostComponent<Input, Timetable, CFtype>
{
public:
    explicit StudentConflicts(const Input& in)
        : CostComponent<Input, Timetable, CFtype>(in, 1000, false, "StudentConflicts") {}

    CFtype ComputeCost(const Timetable& st) const override
    {
        CFtype penalty = 0;
        for (const auto& c : in.conflicts)
            if (st.timeslot_by_exam[c.first] == st.timeslot_by_exam[c.second])
                penalty += c.students;
        return penalty;
    }

    void PrintViolations(const Timetable&, std::ostream&) const override {}
};

class ConsecutiveExams : public CostComponent<Input, Timetable, CFtype>
{
public:
    explicit ConsecutiveExams(const Input& in)
        : CostComponent<Input, Timetable, CFtype>(in, 10, false, "ConsecutiveExams") {}

    CFtype ComputeCost(const Timetable& st) const override
    {
        CFtype penalty = 0;
        for (const auto& c : in.conflicts)
        {
            const auto first = st.timeslot_by_exam[c.first];
            const auto second = st.timeslot_by_exam[c.second];
            const auto distance = first > second ? first - second : second - first;
            if (distance == 1)
                penalty += c.students;
        }
        return penalty;
    }

    void PrintViolations(const Timetable&, std::ostream&) const override {}
};

class TimeslotLoad : public CostComponent<Input, Timetable, CFtype>
{
public:
    explicit TimeslotLoad(const Input& in)
        : CostComponent<Input, Timetable, CFtype>(in, 1, false, "TimeslotLoad") {}

    CFtype ComputeCost(const Timetable& st) const override
    {
        std::vector<CFtype> load(in.timeslot_count, 0);
        for (const auto t : st.timeslot_by_exam)
            ++load[t];
        CFtype penalty = 0;
        for (const auto count : load)
            penalty += count * count;
        return penalty;
    }

    void PrintViolations(const Timetable&, std::ostream&) const override {}
};

class StudentConflictsDelta : public DeltaCostComponent<Input, Timetable, MoveExam, CFtype>
{
public:
    StudentConflictsDelta(const Input& in, StudentConflicts& cc)
        : DeltaCostComponent<Input, Timetable, MoveExam, CFtype>(in, cc, "StudentConflictsDelta") {}

    CFtype ComputeDeltaCost(const Timetable& st, const MoveExam& mv) const override
    {
        const auto source = st.timeslot_by_exam[mv.exam];
        CFtype change = 0;
        for (const auto& c : in.conflicts_by_exam[mv.exam])
        {
            const auto other_timeslot = st.timeslot_by_exam[c.exam];
            if (source == other_timeslot)
                change -= c.students;
            if (mv.destination == other_timeslot)
                change += c.students;
        }
        return change;
    }
};

class ConsecutiveExamsDelta : public DeltaCostComponent<Input, Timetable, MoveExam, CFtype>
{
public:
    ConsecutiveExamsDelta(const Input& in, ConsecutiveExams& cc)
        : DeltaCostComponent<Input, Timetable, MoveExam, CFtype>(in, cc, "ConsecutiveExamsDelta") {}

    CFtype ComputeDeltaCost(const Timetable& st, const MoveExam& mv) const override
    {
        const auto source = st.timeslot_by_exam[mv.exam];
        CFtype change = 0;
        for (const auto& c : in.conflicts_by_exam[mv.exam])
        {
            const auto other_timeslot = st.timeslot_by_exam[c.exam];
            const auto old_distance = source > other_timeslot ? source - other_timeslot : other_timeslot - source;
            const auto new_distance = mv.destination > other_timeslot ? mv.destination - other_timeslot
                                                                       : other_timeslot - mv.destination;
            if (old_distance == 1)
                change -= c.students;
            if (new_distance == 1)
                change += c.students;
        }
        return change;
    }
};

// Port of TimeslotLoadDeltaEvaluator (common/exam_timetabling_deltas.hpp),
// with its O(exams) recount of the timeslot loads: delta mode all.
class TimeslotLoadDelta : public DeltaCostComponent<Input, Timetable, MoveExam, CFtype>
{
public:
    TimeslotLoadDelta(const Input& in, TimeslotLoad& cc)
        : DeltaCostComponent<Input, Timetable, MoveExam, CFtype>(in, cc, "TimeslotLoadDelta") {}

    CFtype ComputeDeltaCost(const Timetable& st, const MoveExam& mv) const override
    {
        const auto source = st.timeslot_by_exam[mv.exam];
        std::vector<CFtype> load(in.timeslot_count, 0);
        for (const auto t : st.timeslot_by_exam)
            ++load[t];
        const auto source_load = load[source];
        const auto destination_load = load[mv.destination];
        const auto before = source_load * source_load + destination_load * destination_load;
        const auto after = (source_load - 1) * (source_load - 1) +
                           (destination_load + 1) * (destination_load + 1);
        return after - before;
    }
};

class MoveExamNeighborhoodExplorer
    : public NeighborhoodExplorer<Input, Timetable, MoveExam, CostStructure>
{
public:
    MoveExamNeighborhoodExplorer(const Input& in, ExamSolutionManager& sm)
        : NeighborhoodExplorer<Input, Timetable, MoveExam, CostStructure>(in, sm, "Move exam") {}

    bool FeasibleMove(const Timetable& st, const MoveExam& mv) const override
    {
        return mv.exam < st.timeslot_by_exam.size() && mv.destination < in.timeslot_count &&
               st.timeslot_by_exam[mv.exam] != mv.destination;
    }

    // Uniform ordinal over exams x (timeslots - 1), skipping the current slot.
    void RandomMove(const Timetable& st, MoveExam& mv) const override
    {
        const auto alternatives = in.timeslot_count > 0 ? in.timeslot_count - 1 : 0;
        const auto count = st.timeslot_by_exam.size() * alternatives;
        if (count == 0)
            throw EmptyNeighborhood();
        const auto ordinal = Random::Uniform<std::size_t>(0, count - 1);
        mv.exam = ordinal / alternatives;
        const auto offset = ordinal % alternatives;
        const auto current = st.timeslot_by_exam[mv.exam];
        mv.destination = offset < current ? offset : offset + 1;
    }

    void FirstMove(const Timetable& st, MoveExam& mv) const override
    {
        if (st.timeslot_by_exam.empty() || in.timeslot_count < 2)
            throw EmptyNeighborhood();
        mv.exam = 0;
        mv.destination = FirstDestination(st, 0);
    }

    bool NextMove(const Timetable& st, MoveExam& mv) const override
    {
        const auto current = st.timeslot_by_exam[mv.exam];
        for (auto destination = mv.destination + 1; destination < in.timeslot_count; ++destination)
            if (destination != current)
            {
                mv.destination = destination;
                return true;
            }
        if (mv.exam + 1 < st.timeslot_by_exam.size())
        {
            mv.exam = mv.exam + 1;
            mv.destination = FirstDestination(st, mv.exam);
            return true;
        }
        return false;
    }

    void MakeMove(Timetable& st, const MoveExam& mv) const override
    {
        st.timeslot_by_exam[mv.exam] = mv.destination;
    }

private:
    static std::size_t FirstDestination(const Timetable& st, std::size_t exam)
    {
        return st.timeslot_by_exam[exam] == 0 ? 1 : 0;
    }
};

} // namespace exam
