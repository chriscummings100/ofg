//! Browser-free OFG core state for the bootstrap vertical slice.

use serde::{Deserialize, Serialize};

/// Minimal deterministic frame state shared by native tests and browser WASM.
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub struct FrameState {
    frame_count: u64,
    last_time_ms: f64,
}

impl FrameState {
    /// Creates a frame state before any browser or smoke frames have rendered.
    pub fn new() -> Self {
        Self {
            frame_count: 0,
            last_time_ms: 0.0,
        }
    }

    /// Records one requested frame and keeps the last finite timestamp.
    pub fn tick(&mut self, time_ms: f64) {
        self.frame_count = self.frame_count.saturating_add(1);
        if time_ms.is_finite() {
            self.last_time_ms = time_ms;
        }
    }

    /// Number of frame requests accepted by the runtime.
    pub fn frame_count(&self) -> u64 {
        self.frame_count
    }

    /// Last finite timestamp passed to [`FrameState::tick`].
    pub fn last_time_ms(&self) -> f64 {
        self.last_time_ms
    }
}

impl Default for FrameState {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::FrameState;

    #[test]
    fn frame_state_counts_frames_and_keeps_last_finite_time() {
        let mut state = FrameState::new();

        state.tick(16.0);
        state.tick(f64::NAN);
        state.tick(48.5);

        assert_eq!(state.frame_count(), 3);
        assert_eq!(state.last_time_ms(), 48.5);
    }

    #[test]
    fn default_frame_state_matches_new_state() {
        assert_eq!(FrameState::default(), FrameState::new());
    }
}
