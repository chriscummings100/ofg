export function computeFrameDeltaSeconds(
  timestamp: number,
  previousTimestamp: number,
  maxDeltaSeconds = 0.05
): number {
  const deltaSeconds = (timestamp - previousTimestamp) / 1000;
  if (!Number.isFinite(deltaSeconds) || deltaSeconds < 0) {
    return 0;
  }

  return Math.min(maxDeltaSeconds, deltaSeconds);
}
