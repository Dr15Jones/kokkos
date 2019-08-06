/*
//@HEADER
// ************************************************************************
//
//                        Kokkos v. 2.0
//              Copyright (2014) Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Christian R. Trott (crtrott@sandia.gov)
//
// ************************************************************************
//@HEADER
*/

#ifndef KOKKOS_TBB_TASK_HPP
#define KOKKOS_TBB_TASK_HPP

#include <Kokkos_Macros.hpp>
#if defined(KOKKOS_ENABLE_TBB) && defined(KOKKOS_ENABLE_TASKDAG)

#include <Kokkos_TaskScheduler_fwd.hpp>

#include <Kokkos_TBB.hpp>

#include <type_traits>

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

namespace Kokkos {
namespace Impl {

template <class QueueType>
class TaskQueueSpecialization<
    SimpleTaskScheduler<Kokkos::Experimental::TBB, QueueType>> {
public:
  using execution_space = Kokkos::Experimental::TBB;
  using scheduler_type =
      SimpleTaskScheduler<Kokkos::Experimental::TBB, QueueType>;
  using member_type =
      TaskTeamMemberAdapter<Kokkos::Impl::TBBTeamMember, scheduler_type>;
  using memory_space = Kokkos::HostSpace;

  /*
  static void execute(scheduler_type const &scheduler) {
    // NOTE: We create an instance so that we can use dispatch_execute_task.
    // This is not necessarily the most efficient, but can be improved later.
    TaskQueueSpecialization<scheduler_type> task_queue;
    task_queue.scheduler = &scheduler;
    Kokkos::Impl::dispatch_execute_task(&task_queue);
    Kokkos::Experimental::TBB().fence();
  }

  // Must provide task queue execution function
  void execute_task() const {
    using hpx::apply;
    using hpx::lcos::local::counting_semaphore;
    using task_base_type = typename scheduler_type::task_base_type;

    const int num_worker_threads = Kokkos::Experimental::TBB::concurrency();

    thread_buffer &buffer = Kokkos::Experimental::TBB::impl_get_buffer();
    buffer.resize(num_worker_threads, 512);

    auto &queue = scheduler->queue();

    counting_semaphore sem(0);

    for (int thread = 0; thread < num_worker_threads; ++thread) {
      apply([this, &sem, &queue, &buffer, num_worker_threads, thread]() {
        // NOTE: This implementation has been simplified based on the
        // assumption that team_size = 1. The TBB backend currently only
        // supports a team size of 1.
        std::size_t t = Kokkos::Experimental::TBB::impl_hardware_thread_id();

        buffer.get(Kokkos::Experimental::TBB::impl_hardware_thread_id());
        TBBTeamMember member(TeamPolicyInternal<Kokkos::Experimental::TBB>(
                                 Kokkos::Experimental::TBB(), num_worker_threads, 1),
                             0, t, buffer.get(t), 512);

        member_type single_exec(*scheduler, member);
        member_type &team_exec = single_exec;

        auto &team_scheduler = team_exec.scheduler();
        auto current_task = OptionalRef<task_base_type>(nullptr);

        while (!queue.is_done()) {
          current_task =
              queue.pop_ready_task(team_scheduler.team_scheduler_info());

          if (current_task) {
            KOKKOS_ASSERT(current_task->is_single_runnable() ||
                          current_task->is_team_runnable());
            current_task->as_runnable_task().run(single_exec);
            queue.complete((*std::move(current_task)).as_runnable_task(),
                           team_scheduler.team_scheduler_info());
          }
        }

        sem.signal(1);
      });
    }

    sem.wait(num_worker_threads);
  }
  */
  static uint32_t get_max_team_count(execution_space const &espace) {
    return static_cast<uint32_t>(espace.concurrency());
  }

  template <typename TaskType>
  static void get_function_pointer(typename TaskType::function_type &ptr,
                                   typename TaskType::destroy_type &dtor) {
    ptr = TaskType::apply;
    dtor = TaskType::destroy;
  }

private:
  const scheduler_type *scheduler;
};

template <class Scheduler>
class TaskQueueSpecializationConstrained<
    Scheduler, typename std::enable_if<
                   std::is_same<typename Scheduler::execution_space,
                                Kokkos::Experimental::TBB>::value>::type> {
public:
  using execution_space = Kokkos::Experimental::TBB;
  using scheduler_type = Scheduler;
  using member_type =
      TaskTeamMemberAdapter<Kokkos::Impl::TBBTeamMember, scheduler_type>;
  using memory_space = Kokkos::HostSpace;

  static void
  iff_single_thread_recursive_execute(scheduler_type const &scheduler) {
    using task_base_type = typename scheduler_type::task_base;
    using queue_type = typename scheduler_type::queue_type;

    if (1 == Kokkos::Experimental::TBB::concurrency()) {
      task_base_type *const end = (task_base_type *)task_base_type::EndTag;
      task_base_type *task = end;

      TBBTeamMember member(TeamPolicyInternal<Kokkos::Experimental::TBB>(
                               Kokkos::Experimental::TBB(), 1, 1),
                           0, 0, nullptr, 0);
      member_type single_exec(scheduler, member);

      do {
        task = end;

        // Loop by priority and then type
        for (int i = 0; i < queue_type::NumQueue && end == task; ++i) {
          for (int j = 0; j < 2 && end == task; ++j) {
            task =
                queue_type::pop_ready_task(&scheduler.m_queue->m_ready[i][j]);
          }
        }

        if (end == task)
          break;

        (*task->m_apply)(task, &single_exec);

        scheduler.m_queue->complete(task);

      } while (true);
    }
  }

  /*  static void execute(scheduler_type const &scheduler) {
    // NOTE: We create an instance so that we can use dispatch_execute_task.
    // This is not necessarily the most efficient, but can be improved later.
    TaskQueueSpecializationConstrained<scheduler_type> task_queue;
    task_queue.scheduler = &scheduler;
    Kokkos::Impl::dispatch_execute_task(&task_queue);
    Kokkos::Experimental::TBB().fence();
  }

  // Must provide task queue execution function
  void execute_task() const {
    using hpx::apply;
    using hpx::lcos::local::counting_semaphore;
    using task_base_type = typename scheduler_type::task_base;
    using queue_type = typename scheduler_type::queue_type;

    const int num_worker_threads = Kokkos::Experimental::TBB::concurrency();
    static task_base_type *const end = (task_base_type *)task_base_type::EndTag;
    constexpr task_base_type *no_more_tasks_sentinel = nullptr;

    thread_buffer &buffer = Kokkos::Experimental::TBB::impl_get_buffer();
    buffer.resize(num_worker_threads, 512);

    auto &queue = scheduler->queue();
    queue.initialize_team_queues(num_worker_threads);

    counting_semaphore sem(0);

    for (int thread = 0; thread < num_worker_threads; ++thread) {
      apply([this, &sem, &buffer, num_worker_threads, thread]() {
        // NOTE: This implementation has been simplified based on the assumption
        // that team_size = 1. The TBB backend currently only supports a team
        // size of 1.
        std::size_t t = Kokkos::Experimental::TBB::impl_hardware_thread_id();

        buffer.get(Kokkos::Experimental::TBB::impl_hardware_thread_id());
        TBBTeamMember member(
            TeamPolicyInternal<Kokkos::Experimental::TBB>(
                Kokkos::Experimental::TBB(), num_worker_threads, 1),
            0, t, buffer.get(t), 512);

        member_type single_exec(*scheduler, member);
        member_type &team_exec = single_exec;

        auto &team_queue = team_exec.scheduler().queue();
        task_base_type *task = no_more_tasks_sentinel;

        do {
          if (task != no_more_tasks_sentinel && task != end) {
            team_queue.complete(task);
          }

          if (*((volatile int *)&team_queue.m_ready_count) > 0) {
            task = end;
            for (int i = 0; i < queue_type::NumQueue && end == task; ++i) {
              for (int j = 0; j < 2 && end == task; ++j) {
                task = queue_type::pop_ready_task(&team_queue.m_ready[i][j]);
              }
            }
          } else {
            task = team_queue.attempt_to_steal_task();
          }

          if (task != no_more_tasks_sentinel && task != end) {
            (*task->m_apply)(task, &single_exec);
          }
        } while (task != no_more_tasks_sentinel);

        sem.signal(1);
      });
    }

    sem.wait(num_worker_threads);
  }
  */

  template <typename TaskType>
  static void get_function_pointer(typename TaskType::function_type &ptr,
                                   typename TaskType::destroy_type &dtor) {
    ptr = TaskType::apply;
    dtor = TaskType::destroy;
  }

private:
  const scheduler_type *scheduler;
};

extern template class TaskQueue<
    Kokkos::Experimental::TBB,
    typename Kokkos::Experimental::TBB::memory_space>;

} // namespace Impl
} // namespace Kokkos

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

#endif /* #if defined( KOKKOS_ENABLE_TASKDAG ) */
#endif /* #ifndef KOKKOS_TBB_TASK_HPP */