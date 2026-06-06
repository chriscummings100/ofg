// Browser byte-asset bridge for Rust-owned model loading.
// TypeScript fetches opaque bytes through assetLoader.loadBytes; Rust remains
// responsible for interpreting the GLB/glTF model data.

#[cfg(target_arch = "wasm32")]
use std::fmt;

#[cfg(target_arch = "wasm32")]
pub async fn load_model_asset_bytes(
    asset_loader: &wasm_bindgen::JsValue,
    id: &str,
    url: &str,
) -> Result<Vec<u8>, wasm_bindgen::JsValue> {
    use wasm_bindgen::JsCast;
    use wasm_bindgen_futures::JsFuture;

    let request_value = byte_asset_requests_to_js(id, url)?;
    let loader_method =
        js_sys::Reflect::get(asset_loader, &wasm_bindgen::JsValue::from_str("loadBytes"))
            .map_err(|_| js_error("Rust browser game could not read assetLoader.loadBytes."))?;
    let loader_function = loader_method.dyn_into::<js_sys::Function>().map_err(|_| {
        js_error("Rust browser game expected assetLoader.loadBytes to be a function.")
    })?;
    let promise_value = loader_function.call1(asset_loader, &request_value)?;
    let promise = promise_value
        .dyn_into::<js_sys::Promise>()
        .map_err(|_| js_error("Rust browser game expected loadBytes to return a Promise."))?;
    let assets_value = JsFuture::from(promise).await?;
    byte_asset_from_js(&assets_value, id)
}

#[cfg(target_arch = "wasm32")]
fn byte_asset_requests_to_js(
    id: &str,
    url: &str,
) -> Result<wasm_bindgen::JsValue, wasm_bindgen::JsValue> {
    let array = js_sys::Array::new();
    let object = js_sys::Object::new();
    set_js_property(&object, "id", wasm_bindgen::JsValue::from_str(id))?;
    set_js_property(&object, "url", wasm_bindgen::JsValue::from_str(url))?;
    array.push(&object);
    Ok(array.into())
}

#[cfg(target_arch = "wasm32")]
fn byte_asset_from_js(
    value: &wasm_bindgen::JsValue,
    expected_id: &str,
) -> Result<Vec<u8>, wasm_bindgen::JsValue> {
    use wasm_bindgen::JsCast;

    let Some(array) = value.dyn_ref::<js_sys::Array>() else {
        return Err(js_error(
            "Rust browser game expected loadBytes to resolve to an array.",
        ));
    };
    if array.length() != 1 {
        return Err(js_error(format!(
            "Rust browser game expected loadBytes to resolve one asset, received {}.",
            array.length()
        )));
    }

    let item = array.get(0);
    let id = js_required_string(&item, "id", "loadBytes[0].id")?;
    if id != expected_id {
        return Err(js_error(format!(
            "Rust browser game expected byte asset '{expected_id}', received '{id}'."
        )));
    }
    let data_value = js_required_property(&item, "data", "loadBytes[0].data")?;
    let data_array = data_value.dyn_ref::<js_sys::Uint8Array>().ok_or_else(|| {
        js_error("Rust browser game expected loadBytes[0].data to be a Uint8Array.")
    })?;
    let mut data = vec![0; data_array.length() as usize];
    data_array.copy_to(&mut data);

    Ok(data)
}

#[cfg(target_arch = "wasm32")]
fn js_required_property(
    object: &wasm_bindgen::JsValue,
    property: &str,
    path: &str,
) -> Result<wasm_bindgen::JsValue, wasm_bindgen::JsValue> {
    let value = js_sys::Reflect::get(object, &wasm_bindgen::JsValue::from_str(property))
        .map_err(|_| js_error(format!("Rust browser game could not read {path}.")))?;
    if value.is_null() || value.is_undefined() {
        return Err(js_error(format!("Rust browser game expected {path}.")));
    }

    Ok(value)
}

#[cfg(target_arch = "wasm32")]
fn js_required_string(
    object: &wasm_bindgen::JsValue,
    property: &str,
    path: &str,
) -> Result<String, wasm_bindgen::JsValue> {
    let value = js_required_property(object, property, path)?;
    value
        .as_string()
        .ok_or_else(|| js_error(format!("Rust browser game expected {path} to be a string.")))
}

#[cfg(target_arch = "wasm32")]
fn set_js_property(
    object: &js_sys::Object,
    property: &str,
    value: wasm_bindgen::JsValue,
) -> Result<(), wasm_bindgen::JsValue> {
    js_sys::Reflect::set(object, &wasm_bindgen::JsValue::from_str(property), &value)
        .map_err(|_| js_error(format!("Rust browser game could not set '{property}'.")))?;
    Ok(())
}

#[cfg(target_arch = "wasm32")]
fn js_error(error: impl fmt::Display) -> wasm_bindgen::JsValue {
    use wasm_bindgen::JsCast;

    js_sys::Error::new(&error.to_string()).unchecked_into()
}
