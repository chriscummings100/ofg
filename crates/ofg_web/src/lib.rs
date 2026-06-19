//! Browser-facing Rust/WASM facade for the OFG bootstrap renderer.

mod status;

pub use status::RuntimeDebugStatus;

#[cfg(target_arch = "wasm32")]
mod browser;

#[cfg(target_arch = "wasm32")]
pub use browser::BrowserGame;

#[cfg(all(test, not(target_arch = "wasm32")))]
mod tests {
    use super::RuntimeDebugStatus;

    #[test]
    fn status_json_contains_browser_contract_fields() {
        let status = RuntimeDebugStatus::uninitialized("waiting for test");
        let json = status.to_json();

        assert!(json.contains("\"initialized\":false"));
        assert!(json.contains("\"frameCount\":0"));
        assert!(json.contains("\"canvasWidth\":0"));
        assert!(json.contains("\"lastError\":\"waiting for test\""));
    }
}

#[cfg(all(test, target_arch = "wasm32"))]
mod wasm_tests {
    use super::RuntimeDebugStatus;
    use wasm_bindgen_test::wasm_bindgen_test;

    #[wasm_bindgen_test]
    fn wasm_status_json_contains_browser_contract_fields() {
        let status = RuntimeDebugStatus::uninitialized("waiting for wasm test");
        let json = status.to_json();

        assert!(json.contains("\"initialized\":false"));
        assert!(json.contains("\"frameCount\":0"));
        assert!(json.contains("\"canvasWidth\":0"));
        assert!(json.contains("\"lastError\":\"waiting for wasm test\""));
    }
}
