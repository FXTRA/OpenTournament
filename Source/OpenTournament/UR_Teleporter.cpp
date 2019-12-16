// Copyright 2019 Open Tournament Project, All Rights Reserved.

/////////////////////////////////////////////////////////////////////////////////////////////////

#include "UR_Teleporter.h"

#include "Engine/Engine.h"
#include "Components/ArrowComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystemComponent.h"

#include "OpenTournament.h"
#include "UR_Character.h"
#include "UR_CharacterMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AutomationTest.h"
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////

// Sets default values
AUR_Teleporter::AUR_Teleporter(const FObjectInitializer& ObjectInitializer) :
    Super(ObjectInitializer),
    DestinationActor(nullptr),
    ExitRotationType(EExitRotation::ER_Relative),
    bKeepMomentum(true),
    TeleportOutSound(nullptr),
    TeleportInSound(nullptr)
{
    // Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
    PrimaryActorTick.bCanEverTick = false;
    PrimaryActorTick.bStartWithTickEnabled = false;

    CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CapsuleComponent"));
    CapsuleComponent->SetCapsuleSize(45.f, 90.f, false);
    SetRootComponent(CapsuleComponent);
    CapsuleComponent->SetGenerateOverlapEvents(true);
    CapsuleComponent->OnComponentBeginOverlap.AddDynamic(this, &AUR_Teleporter::OnTriggerEnter);

    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMeshComponent"));
    MeshComponent->SetupAttachment(RootComponent);

#if WITH_EDITORONLY_DATA
    ArrowComponent = CreateDefaultSubobject<UArrowComponent>(TEXT("ArrowComponent"));
    ArrowComponent->SetupAttachment(CapsuleComponent);
#endif

    AudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("AudioComponent"));
    AudioComponent->SetupAttachment(CapsuleComponent);

    ParticleSystemComponent = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("ParticleSystemComponent"));
    ParticleSystemComponent->SetupAttachment(RootComponent);
}

/////////////////////////////////////////////////////////////////////////////////////////////////

// Called when the game starts or when spawned
void AUR_Teleporter::BeginPlay()
{
    Super::BeginPlay();
}

// Called every frame
void AUR_Teleporter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

void AUR_Teleporter::OnTriggerEnter(UPrimitiveComponent* HitComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    // TODO(Pedro): we should store the "teleporting" state in the MovementComponent of the actor in order to query it here
    bool isTeleporting = (bFromSweep == false);

    if (isTeleporting)
    {
        return;
    }

    if (IsPermittedToTeleport(Other))
    {
        GAME_LOG(Game, Verbose, "Teleporter (%s) Triggered", *GetName());

        if (PerformTeleport(Other))
        {
            GAME_LOG(Game, Log, "Teleported Character (%s) to DestinationActor (%s)", *Other->GetName(), *DestinationActor->GetName());
        }
        else
        {
            GAME_LOG(Game, Warning, "Teleporter Destination not set", *Other->GetName(), *DestinationActor->GetName());
        }
    }
}

bool AUR_Teleporter::IsPermittedToTeleport_Implementation(const AActor* TargetActor) const
{
    // @! TODO : Check to see if the component/actor overlapping here matches a LD-specifiable list of classes (e.g. if we want to teleport only characters, or if things such as projectiles, vehicles, etc. may also pass through). This function may also be overridden to determine conditions such as only characters of Red/Blue team may pass through
    const AUR_Character* Character = Cast<AUR_Character>(TargetActor);
    if (Character == nullptr)
    {
        GAME_LOG(Game, Log, "Teleporter Error. Character was invalid.");
        return false;
    }

    return true;
}

bool AUR_Teleporter::PerformTeleport(AActor* TargetActor)
{
    if (DestinationActor == nullptr || TargetActor == nullptr)
    {
        return false;
    }

    AController* CharacterController{ nullptr };
    UPawnMovementComponent* CharacterMovement{ nullptr };
    const auto TargetCharacter{ Cast<ACharacter>(TargetActor) };

    FRotator TargetActorRotation{ FRotator::ZeroRotator };
    FRotator DestinationRotation{ DestinationActor->GetActorRotation() };
    FRotator DesiredRotation{ DestinationRotation };

    if (TargetCharacter)
    {
        CharacterController = TargetCharacter->GetController();
        CharacterMovement = TargetCharacter->GetMovementComponent();
        TargetActorRotation = CharacterController->GetControlRotation();
    }
    else
    {
        TargetActorRotation = TargetActor->GetActorRotation();
    }

    // Play effects associated with teleportation
    PlayTeleportEffects();

    // Move Actor to Destination actor
    TargetActor->SetActorLocation(DestinationActor->GetActorLocation());

    // Find out Desired Rotation
    GetDesiredRotation(DesiredRotation, TargetActorRotation, DestinationRotation);

    // Rotate the TargetActor to face the Exit Direction vector
    if (TargetCharacter)
    {
        CharacterController->SetControlRotation(DesiredRotation);
    }
    else
    {
        TargetActor->SetActorRotation(DesiredRotation);
    }
    
    // Rotate velocity vector relative to the destination teleporter exit heading
    if (!bKeepMomentum)
    {
        if (TargetCharacter)
        {
            CharacterMovement->Velocity = FVector::ZeroVector;
        }
        else
        {
            TargetActor->GetRootComponent()->ComponentVelocity = FVector::ZeroVector;
        }
    }
    else 
    {
        if (ExitRotationType == EExitRotation::ER_Relative)
        {			
            // Rotate velocity vector relatively to the Exit Direction of the Destination actor
            FRotator MomentumRotator = DestinationActor->GetRootComponent()->GetComponentRotation() - GetRootComponent()->GetComponentRotation();
            MomentumRotator.Yaw = FMath::UnwindDegrees(MomentumRotator.Yaw + 180);

            if (TargetCharacter)
            {
                FVector NewTargetVelocity = MomentumRotator.RotateVector(CharacterMovement->Velocity);
                CharacterMovement->Velocity = NewTargetVelocity;
            }
            else
            {
                FVector NewTargetVelocity = MomentumRotator.RotateVector(TargetActor->GetRootComponent()->ComponentVelocity);
                TargetActor->GetRootComponent()->ComponentVelocity = NewTargetVelocity;
            }
        }
        else
        {
            // Rotate velocity vector to face the Exit Direction of the Destination actor
            if (TargetCharacter)
            {
                auto NewTargetVelocity = DestinationRotation.RotateVector(FVector::ForwardVector * CharacterMovement->Velocity.Size2D());
                NewTargetVelocity.Z = CharacterMovement->Velocity.Z;
                CharacterMovement->Velocity = NewTargetVelocity;
            }
            else
            {
                auto NewTargetVelocity = DestinationRotation.RotateVector(FVector::ForwardVector * TargetActor->GetRootComponent()->ComponentVelocity.Size2D());
                NewTargetVelocity.Z = TargetActor->GetRootComponent()->ComponentVelocity.Z;
                TargetActor->GetRootComponent()->ComponentVelocity = NewTargetVelocity;
            }
        }
    }

    return true;
}

void AUR_Teleporter::PlayTeleportEffects_Implementation()
{
    if (TeleportOutSound)
    {
        UGameplayStatics::PlaySoundAtLocation(GetWorld(), TeleportOutSound, GetActorLocation());
    }

    if (TeleportInSound)
    {
        UGameplayStatics::PlaySoundAtLocation(GetWorld(), TeleportInSound, DestinationActor->GetActorLocation());
    }
}

void AUR_Teleporter::GetDesiredRotation(FRotator& DesiredRotation, const FRotator& TargetActorRotation, const FRotator& DestinationRotation)
{
    if (ExitRotationType == EExitRotation::ER_Relative)
    {
        DesiredRotation = DestinationRotation + TargetActorRotation - this->GetActorRotation();
        DesiredRotation.Yaw += 180;
    }
    else
    {
        DesiredRotation = DestinationRotation;
    }

    DesiredRotation.Yaw = FMath::UnwindDegrees(DesiredRotation.Yaw);
    DesiredRotation.Pitch = 0.0f;
    DesiredRotation.Roll = 0.0f;
}

/////////////////////////////////////////////////////////////////////////////////////////////////

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOpenTournamentTeleporterTest, "OpenTournament.Feature.Levels.LevelFeatures.Actor", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FOpenTournamentTeleporterTest::RunTest(const FString& Parameters)
{
    // TODO : Automated Tests

    return true;
}

#endif WITH_DEV_AUTOMATION_TESTS