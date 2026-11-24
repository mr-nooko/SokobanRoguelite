#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GridVisualProfile.generated.h"

class UMaterialInterface;
class UStaticMesh;
class USoundBase;

UENUM(BlueprintType)
enum class EGridVisualRole : uint8
{
    None UMETA(Hidden),
    Floor UMETA(DisplayName="地板"),
    Wall UMETA(DisplayName="墙"),
    Rubble UMETA(DisplayName="碎墙"),
    Crate UMETA(DisplayName="普通箱"),
    SpikeCrate UMETA(DisplayName="尖刺箱"),
    ExplosiveCrate UMETA(DisplayName="爆炸箱"),
    Spike UMETA(DisplayName="尖刺"),
    ContactEnemy UMETA(DisplayName="接触敌人"),
    BeamEnemy UMETA(DisplayName="射线敌人"),
    Goal UMETA(DisplayName="终点"),
    Beam UMETA(DisplayName="光束"),
    EjectCrate UMETA(DisplayName="弹射箱")
};

SOKOBANROGUELITE_API EGridVisualRole GetGridBoxVisualRole(FIntPoint Spike, bool bExplodes, bool bEject);
SOKOBANROGUELITE_API FText GetGridBoxVisualLabel(FIntPoint Spike, bool bExplodes, bool bEject);

UENUM()
enum class EGridTransformOverride : uint8
{
    Compatibility UMETA(DisplayName="自动（兼容已有资产）"),
    Inherit UMETA(DisplayName="继承项目默认配置"),
    Override UMETA(DisplayName="覆盖项目默认配置")
};

UENUM()
enum class EGridSoundCue : uint8
{
    Step UMETA(DisplayName="移动"),
    Push UMETA(DisplayName="推箱"),
    PushRejected UMETA(DisplayName="推动失败"),
    EnemyKilled UMETA(DisplayName="击杀敌人"),
    Blast UMETA(DisplayName="爆炸"),
    Restored UMETA(DisplayName="倒放"),
    Death UMETA(DisplayName="死亡"),
    Goal UMETA(DisplayName="通关")
};

USTRUCT(BlueprintType)
struct SOKOBANROGUELITE_API FGridVisualSlot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category="资源", meta=(DisplayName="静态网格"))
    TObjectPtr<UStaticMesh> Mesh;

    UPROPERTY(EditAnywhere, Category="资源", meta=(DisplayName="材质槽 0 覆盖"))
    TObjectPtr<UMaterialInterface> Material;

    UPROPERTY(EditAnywhere, Category="资源", meta=(DisplayName="交错材质（仅地板）"))
    TObjectPtr<UMaterialInterface> AlternateMaterial;

    UPROPERTY(EditAnywhere, Category="变换", meta=(DisplayName="按单格尺寸自动贴地"))
    bool bFitToCell = false;

    UPROPERTY(EditAnywhere, Category="变换", meta=(DisplayName="偏移来源"))
    EGridTransformOverride OffsetMode = EGridTransformOverride::Compatibility;

    UPROPERTY(EditAnywhere, Category="变换", meta=(DisplayName="局部偏移（厘米）"))
    FVector Offset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category="变换", meta=(DisplayName="缩放来源"))
    EGridTransformOverride ScaleMode = EGridTransformOverride::Compatibility;

    UPROPERTY(EditAnywhere, Category="变换", meta=(DisplayName="缩放倍率"))
    FVector ScaleMultiplier = FVector::OneVector;

    static FVector ResolveOffset(const FGridVisualSlot* Local, const FGridVisualSlot* Default);
    static FVector ResolveScale(const FGridVisualSlot* Local, const FGridVisualSlot* Default);
};

UCLASS(meta=(DisplayName="关卡美术配置"))
class SOKOBANROGUELITE_API UGridVisualProfile : public UDataAsset
{
    GENERATED_BODY()

public:
    UGridVisualProfile();

    UPROPERTY(EditAnywhere, Category="资源", meta=(DisplayName="按用途配置外观"))
    TMap<EGridVisualRole, FGridVisualSlot> Slots;

    UPROPERTY(EditAnywhere, Category="资源", meta=(DisplayName="终点粒子材质"))
    TObjectPtr<UMaterialInterface> GoalParticleMaterial;

    UPROPERTY(EditAnywhere, Category="音效", meta=(DisplayName="按事件配置音效"))
    TMap<EGridSoundCue, TObjectPtr<USoundBase>> Sounds;

    const FGridVisualSlot* FindSlot(EGridVisualRole Role) const { return Slots.Find(Role); }
};
