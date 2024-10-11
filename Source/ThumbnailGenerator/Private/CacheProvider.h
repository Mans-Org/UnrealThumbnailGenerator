// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "Framework/Application/SlateApplication.h"

template<typename HashableKey, typename UObjectValue>
struct TCacheProvider : public FGCObject
{
public:
	struct FCacheEntry
	{
		float LastAccessed;
		int64 MemoryFootprint;
	};
	TMap<HashableKey, FCacheEntry>              CacheEntries;
	TMap<HashableKey, TObjectPtr<UObjectValue>> CacheTable; // GC:d table using AddReferencedObjects

	uint64 TotalMemoryFootprint = 0;

public:

	void ClearCache() 
	{ 
		for (const auto& CacheObjectPair : CacheTable)
			if (IsValid(CacheObjectPair.Value))
				OnItemRemovedFromCache(CacheObjectPair.Value);

		CacheEntries.Empty(); 
		CacheTable.Empty(); 
		TotalMemoryFootprint = 0; 
	}

	void CacheItem(const HashableKey& Key, UObjectValue* InObject)
	{
		const auto CacheSize = MaxCacheSize();
		if (CacheSize <= 0)
			return;

		const auto DataSize = GetItemDataFootprint(InObject);

		// Reserve the expected number of slots required
		if (CacheEntries.Num() == 0)
		{
			const auto ReserveSize = CacheSize / DataSize;
			CacheEntries.Reserve(ReserveSize);
			CacheTable.Reserve(ReserveSize);
		}

		CacheEntries.Add(Key, FCacheEntry{ (float)FSlateApplication::Get().GetCurrentTime(), DataSize });
		CacheTable.Add(Key, InObject);

		TotalMemoryFootprint += DataSize;

		UE_LOG(LogThumbnailGenerator, Verbose, TEXT("%s: Add object %s of size %f (MB) to cache"), *DebugCacheName(), *InObject->GetName(), float(DataSize / 1000000.f));
		UE_LOG(LogThumbnailGenerator, Verbose, TEXT("%s: Total cache size: %f (MB)"), *DebugCacheName(), float(TotalMemoryFootprint / 1000000.f));

		// Clear out old cache
		if (TotalMemoryFootprint > CacheSize && CacheEntries.Num() > 1)
		{
			HashableKey  OldestKey;
			FMemory::Memzero(&OldestKey, sizeof(HashableKey));
			FCacheEntry* OldestCacheEntry = nullptr;
			for (auto It = CacheEntries.CreateIterator(); It; ++It)
			{
				const auto OldestTime = OldestCacheEntry ? OldestCacheEntry->LastAccessed : -1.f;
				if (It.Value().LastAccessed > OldestTime)
				{
					OldestCacheEntry = &It.Value();
					OldestKey        = It.Key();
				}
			}

			if (OldestCacheEntry)
			{
				UE_LOG(LogThumbnailGenerator, Verbose, TEXT("%s: Clear out object of size %f (MB)"), *DebugCacheName(), float(OldestCacheEntry->MemoryFootprint / 1000000.f));

				TotalMemoryFootprint -= OldestCacheEntry->MemoryFootprint;
				CacheEntries.Remove(OldestKey);

				// Run cleanup on cached object
				if (TObjectPtr<UObjectValue>* OldCachedObject = CacheTable.Find(OldestKey))
				{
					if (IsValid(*OldCachedObject))
					{
						UE_LOG(LogThumbnailGenerator, Verbose, TEXT("%s: Remove cached object %s"), *DebugCacheName(), *(*OldCachedObject)->GetName());
						OnItemRemovedFromCache(*OldCachedObject);
					}
				}
				CacheTable.Remove(OldestKey);
			}
		}
	}

	UObjectValue* GetCachedItem(const HashableKey& Key)
	{
		if (MaxCacheSize() <= 0)
			return nullptr;

		if (FCacheEntry* CacheEntry = CacheEntries.Find(Key))
		{
			TObjectPtr<UObjectValue>* CachedObject = CacheTable.Find(Key);
			
			// Double check that the object has not been destroyed
			if (!CachedObject || !IsValid(*CachedObject))
			{
				CacheEntries.Remove(Key);
				CacheTable.Remove(Key);
				return nullptr;
			}

			UE_LOG(LogThumbnailGenerator, Verbose, TEXT("%s: Fetched object %s from cache"), *DebugCacheName(), *(*CachedObject)->GetName());

			CacheEntry->LastAccessed = (float)FSlateApplication::Get().GetCurrentTime();
			return *CachedObject;
		}

		return nullptr;
	}

	virtual int32 MaxCacheSize() = 0;

	virtual int32 GetItemDataFootprint(UObjectValue* InObject) = 0;

	virtual FString DebugCacheName() const { return TEXT("Cached UObject"); }

	virtual void OnItemRemovedFromCache(UObjectValue* InObject) {}

	// ~Begin: FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override { Collector.AddReferencedObjects(CacheTable); }
	virtual FString GetReferencerName() const override { return FString::Printf(TEXT("ThumbnailGeneratorCache[%s]"), *DebugCacheName()); }
	// ~End: FGCObject Interface
};
