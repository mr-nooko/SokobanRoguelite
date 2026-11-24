#pragma once

#include "AssetRegistry/AssetData.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"

template <typename ItemType> class STileView;
class ITableRow;
class STableViewBase;
class SBox;
class SEditableTextBox;

class SGridLevelBrowser final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SGridLevelBrowser) {}
        SLATE_ARGUMENT(TArray<FAssetData>, Entries)
        SLATE_EVENT(FOnClicked, OnClose)
        SLATE_EVENT(FSimpleDelegate, OnDraftDeleted)
        SLATE_EVENT(TDelegate<bool(const FAssetData&, FText&)>, OnOpenDraft)
        SLATE_EVENT(TDelegate<bool(const FString&, FText&)>, OnCreateDraft)
        SLATE_ATTRIBUTE(bool, CanOpenDraft)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args);
    static const FString DraftRoot;

private:
    friend class FGridLevelBrowserTest;
    struct FBrowserItem
    {
        FAssetData Asset;
        FString Folder;
        bool IsFolder() const { return !Folder.IsEmpty(); }
    };
    using FEntry = TSharedPtr<FBrowserItem>;
    void Filter(const FText& Text);
    void Select(FEntry Entry);
    void Open(FEntry Entry);
    FReply OpenSelected();
    FReply LocateSelected();
    FReply DeleteSelected();
    FReply CreateFolder();
    void OpenFolder(const FString& Folder);
    void RefreshItems();
    TSharedRef<ITableRow> MakeTile(FEntry Entry, const TSharedRef<STableViewBase>& Owner);

    TArray<FEntry> Entries;
    TArray<FEntry> VisibleEntries;
    FEntry Selected;
    TSharedPtr<STileView<FEntry>> Tiles;
    TSharedPtr<SBox> Preview;
    TSharedPtr<SEditableTextBox> FolderName;
    FString CurrentFolder = DraftRoot;
    FString Search;
    FText StatusMessage;
    FOnClicked Close;
    FSimpleDelegate DraftDeleted;
    TDelegate<bool(const FAssetData&, FText&)> OpenDraft;
    TDelegate<bool(const FString&, FText&)> CreateDraft;
    TAttribute<bool> CanOpenDraft;
};
