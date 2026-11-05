#pragma once

#include "CoreMinimal.h"

// Owned by the socket thread. A short write never discards the rest of a frame.
class FTangentPendingSend
{
public:
	TArray<uint8> Bytes;
	int32 Offset = 0;
	void Reset() { Bytes.Reset(); Offset = 0; }
	bool IsComplete() const { return Offset == Bytes.Num(); }
	bool Advance(TFunctionRef<bool(const uint8*, int32, int32&)> send)
	{
		if (IsComplete()) return true;
		int32 sent = 0;
		if (!send(Bytes.GetData() + Offset, Bytes.Num() - Offset, sent)) return false;
		if (sent < 0 || sent > Bytes.Num() - Offset) return false;
		Offset += sent;
		return true;
	}
};
