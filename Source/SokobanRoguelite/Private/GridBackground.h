#pragma once

#include "GridGameMode.h"
#include "Async/Async.h"

// Jobs receive copies of rule data. Controller and asset access stays on the game thread.
template<class Work, class Complete>
void DispatchGridWork(AGridPlayerController& Controller, Work Job, Complete Done)
{
    const TWeakObjectPtr<AGridPlayerController> Weak(&Controller);
    Async(EAsyncExecution::ThreadPool, [Weak, Job = MoveTemp(Job), Done = MoveTemp(Done)]() mutable {
        auto Result = Job();
        AsyncTask(ENamedThreads::GameThread, [Weak, Done = MoveTemp(Done), Result = MoveTemp(Result)]() mutable {
            if (auto* Player = Weak.Get(); Player && Player->IsGridBusy())
            {
                Done(*Player, MoveTemp(Result));
            }
        });
    });
}
