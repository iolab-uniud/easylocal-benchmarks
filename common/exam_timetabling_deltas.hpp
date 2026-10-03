// A delta evaluator of the timeslot-load component of the EasyLocal
// exam-timetabling example (examples/exam_timetabling), which binds none since
// the examples have no whole-solution deltas. It is the
// TimeslotLoadDeltaEvaluator the example had until EasyLocal commit 21bc016:
// it recounts the loads of all timeslots in O(exams). The EL3-versus-EL4
// driver binds it in the `all` delta mode; el3_vs_el4/el3/exam.hh has the same
// delta.
#pragma once

#include "exam_timetabling/cost_components.hpp"
#include "exam_timetabling/move.hpp"

#include <cassert>
#include <vector>

namespace benchmarks::exam_timetabling
{

using ::exam_timetabling::ExamTimetable;
using ::exam_timetabling::ExamTimetablingInstance;
using ::exam_timetabling::MoveExam;
using ::exam_timetabling::penalty_type;

class TimeslotLoadDeltaEvaluator
{
public:
    explicit TimeslotLoadDeltaEvaluator(const ExamTimetablingInstance& instance)
        : instance_{instance}
    {
    }

    penalty_type delta_evaluate(const ExamTimetable& solution, const MoveExam& move) const
    {
        assert(move.exam < solution.timeslot_by_exam.size());
        const auto source = solution.timeslot_by_exam[move.exam];
        assert(source != move.destination);
        assert(move.destination < instance_.timeslot_count);

        std::vector<penalty_type> load(instance_.timeslot_count, 0);
        for (const auto timeslot : solution.timeslot_by_exam)
            ++load[timeslot];

        const auto source_load = load[source];
        const auto destination_load = load[move.destination];
        const auto before = source_load * source_load + destination_load * destination_load;
        const auto after = (source_load - 1) * (source_load - 1)
            + (destination_load + 1) * (destination_load + 1);

        return after - before;
    }

private:
    const ExamTimetablingInstance& instance_;
};

} // namespace benchmarks::exam_timetabling
