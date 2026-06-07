// Owns terrain texture manifest interpretation for the browser renderer.
// Browser TypeScript only decodes generic URL lists into RGBA texture arrays;
// this module decides which terrain material maps exist and validates them.

use crate::config::{REQUIRED_TEXTURE_ARRAY_LAYERS, TEXTURE_FORMAT_RGBA8_UNORM};

const TERRAIN_TEXTURE_MANIFEST_JSON: &str =
    include_str!("../../../assets/textures/polyhaven/manifest.json");

pub const TERRAIN_ALBEDO_TEXTURE_ARRAY_ID: &str = "terrain.albedo";
pub const TERRAIN_NORMAL_TEXTURE_ARRAY_ID: &str = "terrain.normal";
pub const TERRAIN_MATERIAL_TEXTURE_ARRAY_ID: &str = "terrain.material";

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TerrainTextureArrayRequest {
    pub id: &'static str,
    pub urls: Vec<String>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RgbaTextureArrayAsset {
    pub id: String,
    pub width: u32,
    pub height: u32,
    pub layers: u32,
    pub data: Vec<u8>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TerrainTextureArrays {
    pub width: u32,
    pub height: u32,
    pub layers: u32,
    pub format_code: u32,
    pub albedo: RgbaTextureArrayAsset,
    pub normal: RgbaTextureArrayAsset,
    pub material: RgbaTextureArrayAsset,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum TerrainTextureError {
    InvalidManifest(String),
    InvalidLayerCount {
        actual: usize,
        expected: u32,
    },
    MissingTexturePath {
        material_index: usize,
        map: &'static str,
    },
    UnknownTextureArray(String),
    DuplicateTextureArray(String),
    MissingTextureArray(&'static str),
    TextureShapeMismatch {
        id: String,
        width: u32,
        height: u32,
        layers: u32,
        expected_width: u32,
        expected_height: u32,
        expected_layers: u32,
    },
    InvalidTextureDataLength {
        id: String,
        actual: usize,
        expected: usize,
    },
}

#[derive(serde::Deserialize)]
struct TerrainTextureManifest {
    materials: Vec<TerrainTextureManifestMaterial>,
}

#[derive(serde::Deserialize)]
struct TerrainTextureManifestMaterial {
    maps: TerrainTextureManifestMaps,
}

#[derive(serde::Deserialize)]
struct TerrainTextureManifestMaps {
    albedo: TerrainTextureManifestMap,
    normal: TerrainTextureManifestMap,
    roughness: TerrainTextureManifestMap,
}

#[derive(serde::Deserialize)]
struct TerrainTextureManifestMap {
    path: String,
}

pub fn terrain_texture_array_requests(
) -> Result<Vec<TerrainTextureArrayRequest>, TerrainTextureError> {
    terrain_texture_array_requests_from_manifest_json(TERRAIN_TEXTURE_MANIFEST_JSON)
}

pub fn terrain_texture_array_requests_from_manifest_json(
    manifest_json: &str,
) -> Result<Vec<TerrainTextureArrayRequest>, TerrainTextureError> {
    let manifest = serde_json::from_str::<TerrainTextureManifest>(manifest_json)
        .map_err(|error| TerrainTextureError::InvalidManifest(error.to_string()))?;
    if manifest.materials.len() != REQUIRED_TEXTURE_ARRAY_LAYERS as usize {
        return Err(TerrainTextureError::InvalidLayerCount {
            actual: manifest.materials.len(),
            expected: REQUIRED_TEXTURE_ARRAY_LAYERS,
        });
    }

    let mut albedo = Vec::with_capacity(manifest.materials.len());
    let mut normal = Vec::with_capacity(manifest.materials.len());
    let mut material = Vec::with_capacity(manifest.materials.len());
    for (index, layer) in manifest.materials.iter().enumerate() {
        albedo.push(texture_asset_url(required_path(
            index,
            "albedo",
            &layer.maps.albedo.path,
        )?));
        normal.push(texture_asset_url(required_path(
            index,
            "normal",
            &layer.maps.normal.path,
        )?));
        material.push(texture_asset_url(required_path(
            index,
            "roughness",
            &layer.maps.roughness.path,
        )?));
    }

    Ok(vec![
        TerrainTextureArrayRequest {
            id: TERRAIN_ALBEDO_TEXTURE_ARRAY_ID,
            urls: albedo,
        },
        TerrainTextureArrayRequest {
            id: TERRAIN_NORMAL_TEXTURE_ARRAY_ID,
            urls: normal,
        },
        TerrainTextureArrayRequest {
            id: TERRAIN_MATERIAL_TEXTURE_ARRAY_ID,
            urls: material,
        },
    ])
}

impl TerrainTextureArrays {
    pub fn from_assets(
        assets: Vec<RgbaTextureArrayAsset>,
    ) -> Result<TerrainTextureArrays, TerrainTextureError> {
        let mut albedo = None;
        let mut normal = None;
        let mut material = None;

        for asset in assets {
            validate_texture_asset_data(&asset)?;
            match asset.id.as_str() {
                TERRAIN_ALBEDO_TEXTURE_ARRAY_ID => insert_asset(&mut albedo, asset)?,
                TERRAIN_NORMAL_TEXTURE_ARRAY_ID => insert_asset(&mut normal, asset)?,
                TERRAIN_MATERIAL_TEXTURE_ARRAY_ID => insert_asset(&mut material, asset)?,
                id => return Err(TerrainTextureError::UnknownTextureArray(id.to_string())),
            }
        }

        let albedo = albedo.ok_or(TerrainTextureError::MissingTextureArray(
            TERRAIN_ALBEDO_TEXTURE_ARRAY_ID,
        ))?;
        let normal = normal.ok_or(TerrainTextureError::MissingTextureArray(
            TERRAIN_NORMAL_TEXTURE_ARRAY_ID,
        ))?;
        let material = material.ok_or(TerrainTextureError::MissingTextureArray(
            TERRAIN_MATERIAL_TEXTURE_ARRAY_ID,
        ))?;

        validate_texture_asset_shape(&normal, &albedo)?;
        validate_texture_asset_shape(&material, &albedo)?;

        let width = albedo.width;
        let height = albedo.height;
        let layers = albedo.layers;
        Ok(Self {
            width,
            height,
            layers,
            format_code: TEXTURE_FORMAT_RGBA8_UNORM,
            albedo,
            normal,
            material,
        })
    }
}

impl std::fmt::Display for TerrainTextureError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::InvalidManifest(error) => {
                write!(formatter, "invalid terrain texture manifest: {error}")
            }
            Self::InvalidLayerCount { actual, expected } => write!(
                formatter,
                "terrain texture manifest has {actual} material layers; expected {expected}"
            ),
            Self::MissingTexturePath {
                material_index,
                map,
            } => write!(
                formatter,
                "terrain texture manifest material {material_index} is missing a {map} map path"
            ),
            Self::UnknownTextureArray(id) => {
                write!(formatter, "browser returned unknown texture array '{id}'")
            }
            Self::DuplicateTextureArray(id) => {
                write!(formatter, "browser returned duplicate texture array '{id}'")
            }
            Self::MissingTextureArray(id) => {
                write!(formatter, "browser did not return texture array '{id}'")
            }
            Self::TextureShapeMismatch {
                id,
                width,
                height,
                layers,
                expected_width,
                expected_height,
                expected_layers,
            } => write!(
                formatter,
                "texture array '{id}' has shape {width}x{height}x{layers}; expected {expected_width}x{expected_height}x{expected_layers}"
            ),
            Self::InvalidTextureDataLength {
                id,
                actual,
                expected,
            } => write!(
                formatter,
                "texture array '{id}' has {actual} bytes; expected {expected}"
            ),
        }
    }
}

impl std::error::Error for TerrainTextureError {}

fn required_path<'a>(
    material_index: usize,
    map: &'static str,
    path: &'a str,
) -> Result<&'a str, TerrainTextureError> {
    if path.is_empty() {
        return Err(TerrainTextureError::MissingTexturePath {
            material_index,
            map,
        });
    }

    Ok(path)
}

fn texture_asset_url(path: &str) -> String {
    if path.starts_with('/') {
        path.to_string()
    } else {
        format!("/{path}")
    }
}

fn insert_asset(
    slot: &mut Option<RgbaTextureArrayAsset>,
    asset: RgbaTextureArrayAsset,
) -> Result<(), TerrainTextureError> {
    if slot.is_some() {
        return Err(TerrainTextureError::DuplicateTextureArray(asset.id));
    }

    *slot = Some(asset);
    Ok(())
}

fn validate_texture_asset_shape(
    actual: &RgbaTextureArrayAsset,
    expected: &RgbaTextureArrayAsset,
) -> Result<(), TerrainTextureError> {
    if actual.width == expected.width
        && actual.height == expected.height
        && actual.layers == expected.layers
    {
        return Ok(());
    }

    Err(TerrainTextureError::TextureShapeMismatch {
        id: actual.id.clone(),
        width: actual.width,
        height: actual.height,
        layers: actual.layers,
        expected_width: expected.width,
        expected_height: expected.height,
        expected_layers: expected.layers,
    })
}

fn validate_texture_asset_data(asset: &RgbaTextureArrayAsset) -> Result<(), TerrainTextureError> {
    let expected_len = asset.width as usize * asset.height as usize * asset.layers as usize * 4;
    if asset.width == 0
        || asset.height == 0
        || asset.layers == 0
        || asset.layers != REQUIRED_TEXTURE_ARRAY_LAYERS
        || asset.data.len() != expected_len
    {
        return Err(TerrainTextureError::InvalidTextureDataLength {
            id: asset.id.clone(),
            actual: asset.data.len(),
            expected: expected_len,
        });
    }

    Ok(())
}

#[cfg(target_arch = "wasm32")]
pub async fn load_terrain_texture_arrays(
    asset_loader: &wasm_bindgen::JsValue,
) -> Result<TerrainTextureArrays, wasm_bindgen::JsValue> {
    use wasm_bindgen::JsCast;
    use wasm_bindgen_futures::JsFuture;

    let requests = terrain_texture_array_requests().map_err(js_error)?;
    let request_value = texture_array_requests_to_js(&requests)?;
    let loader_method = js_sys::Reflect::get(
        asset_loader,
        &wasm_bindgen::JsValue::from_str("loadTextureArrays"),
    )
    .map_err(|_| js_error("Rust browser game could not read assetLoader.loadTextureArrays."))?;
    let loader_function = loader_method.dyn_into::<js_sys::Function>().map_err(|_| {
        js_error("Rust browser game expected assetLoader.loadTextureArrays to be a function.")
    })?;
    let promise_value = loader_function.call1(asset_loader, &request_value)?;
    let promise = promise_value.dyn_into::<js_sys::Promise>().map_err(|_| {
        js_error("Rust browser game expected loadTextureArrays to return a Promise.")
    })?;
    let assets_value = JsFuture::from(promise).await?;
    let assets = texture_array_assets_from_js(&assets_value)?;

    TerrainTextureArrays::from_assets(assets).map_err(js_error)
}

#[cfg(target_arch = "wasm32")]
fn texture_array_requests_to_js(
    requests: &[TerrainTextureArrayRequest],
) -> Result<wasm_bindgen::JsValue, wasm_bindgen::JsValue> {
    let array = js_sys::Array::new();
    for request in requests {
        let object = js_sys::Object::new();
        set_js_property(&object, "id", wasm_bindgen::JsValue::from_str(request.id))?;

        let urls = js_sys::Array::new();
        for url in &request.urls {
            urls.push(&wasm_bindgen::JsValue::from_str(url));
        }
        set_js_property(&object, "urls", urls.into())?;
        array.push(&object);
    }

    Ok(array.into())
}

#[cfg(target_arch = "wasm32")]
fn texture_array_assets_from_js(
    value: &wasm_bindgen::JsValue,
) -> Result<Vec<RgbaTextureArrayAsset>, wasm_bindgen::JsValue> {
    use wasm_bindgen::JsCast;

    let Some(array) = value.dyn_ref::<js_sys::Array>() else {
        return Err(js_error(
            "Rust browser game expected loadTextureArrays to resolve to an array.",
        ));
    };
    let mut assets = Vec::with_capacity(array.length() as usize);

    for index in 0..array.length() {
        let item = array.get(index);
        let path = format!("loadTextureArrays[{index}]");
        let id = js_required_string(&item, "id", &format!("{path}.id"))?;
        let width = js_required_u32(&item, "width", &format!("{path}.width"))?;
        let height = js_required_u32(&item, "height", &format!("{path}.height"))?;
        let layers = js_required_u32(&item, "layers", &format!("{path}.layers"))?;
        let data_value = js_required_property(&item, "data", &format!("{path}.data"))?;
        let data_array = data_value.dyn_ref::<js_sys::Uint8Array>().ok_or_else(|| {
            js_error(format!(
                "Rust browser game expected {path}.data to be a Uint8Array."
            ))
        })?;
        let mut data = vec![0; data_array.length() as usize];
        data_array.copy_to(&mut data);

        assets.push(RgbaTextureArrayAsset {
            id,
            width,
            height,
            layers,
            data,
        });
    }

    Ok(assets)
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
fn js_required_u32(
    object: &wasm_bindgen::JsValue,
    property: &str,
    path: &str,
) -> Result<u32, wasm_bindgen::JsValue> {
    let value = js_required_property(object, property, path)?;
    let Some(number) = value.as_f64() else {
        return Err(js_error(format!(
            "Rust browser game expected {path} to be a number."
        )));
    };
    if !number.is_finite() || number.fract() != 0.0 || number < 0.0 || number > u32::MAX as f64 {
        return Err(js_error(format!(
            "Rust browser game expected {path} to be a u32."
        )));
    }

    Ok(number as u32)
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
fn js_error(error: impl std::fmt::Display) -> wasm_bindgen::JsValue {
    use wasm_bindgen::JsCast;

    js_sys::Error::new(&error.to_string()).unchecked_into()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builds_texture_array_requests_from_manifest_paths() {
        let requests = terrain_texture_array_requests_from_manifest_json(&manifest_json())
            .expect("manifest should be valid");

        assert_eq!(requests.len(), 3);
        assert_eq!(requests[0].id, TERRAIN_ALBEDO_TEXTURE_ARRAY_ID);
        assert_eq!(requests[1].id, TERRAIN_NORMAL_TEXTURE_ARRAY_ID);
        assert_eq!(requests[2].id, TERRAIN_MATERIAL_TEXTURE_ARRAY_ID);
        assert_eq!(
            requests[0].urls.len(),
            REQUIRED_TEXTURE_ARRAY_LAYERS as usize
        );
        assert_eq!(requests[0].urls[0], "/textures/albedo-0.png");
        assert_eq!(requests[1].urls[1], "/textures/normal-1.png");
        assert_eq!(requests[2].urls[2], "/textures/roughness-2.png");
    }

    #[test]
    fn rejects_invalid_manifests_and_missing_map_paths() {
        assert!(matches!(
            terrain_texture_array_requests_from_manifest_json("{"),
            Err(TerrainTextureError::InvalidManifest(_))
        ));
        assert_eq!(
            terrain_texture_array_requests_from_manifest_json(&manifest_json_with_layers(2)),
            Err(TerrainTextureError::InvalidLayerCount {
                actual: 2,
                expected: REQUIRED_TEXTURE_ARRAY_LAYERS,
            })
        );
        assert_eq!(
            terrain_texture_array_requests_from_manifest_json(&manifest_json_with_missing_path(
                3, "normal"
            )),
            Err(TerrainTextureError::MissingTexturePath {
                material_index: 3,
                map: "normal",
            })
        );
    }

    #[test]
    fn accepts_complete_browser_texture_array_assets() {
        let arrays = TerrainTextureArrays::from_assets(vec![
            texture_asset(
                TERRAIN_MATERIAL_TEXTURE_ARRAY_ID,
                2,
                2,
                REQUIRED_TEXTURE_ARRAY_LAYERS,
            ),
            texture_asset(
                TERRAIN_ALBEDO_TEXTURE_ARRAY_ID,
                2,
                2,
                REQUIRED_TEXTURE_ARRAY_LAYERS,
            ),
            texture_asset(
                TERRAIN_NORMAL_TEXTURE_ARRAY_ID,
                2,
                2,
                REQUIRED_TEXTURE_ARRAY_LAYERS,
            ),
        ])
        .expect("assets should be valid");

        assert_eq!(arrays.width, 2);
        assert_eq!(arrays.height, 2);
        assert_eq!(arrays.layers, REQUIRED_TEXTURE_ARRAY_LAYERS);
        assert_eq!(arrays.format_code, TEXTURE_FORMAT_RGBA8_UNORM);
        assert_eq!(arrays.albedo.id, TERRAIN_ALBEDO_TEXTURE_ARRAY_ID);
        assert_eq!(arrays.normal.id, TERRAIN_NORMAL_TEXTURE_ARRAY_ID);
        assert_eq!(arrays.material.id, TERRAIN_MATERIAL_TEXTURE_ARRAY_ID);
    }

    #[test]
    fn rejects_unknown_duplicate_and_missing_texture_arrays() {
        assert_eq!(
            TerrainTextureArrays::from_assets(vec![
                texture_asset("terrain.unknown", 1, 1, REQUIRED_TEXTURE_ARRAY_LAYERS),
                texture_asset(
                    TERRAIN_ALBEDO_TEXTURE_ARRAY_ID,
                    1,
                    1,
                    REQUIRED_TEXTURE_ARRAY_LAYERS
                ),
                texture_asset(
                    TERRAIN_NORMAL_TEXTURE_ARRAY_ID,
                    1,
                    1,
                    REQUIRED_TEXTURE_ARRAY_LAYERS
                ),
            ]),
            Err(TerrainTextureError::UnknownTextureArray(
                "terrain.unknown".into()
            ))
        );
        assert_eq!(
            TerrainTextureArrays::from_assets(vec![
                texture_asset(
                    TERRAIN_ALBEDO_TEXTURE_ARRAY_ID,
                    1,
                    1,
                    REQUIRED_TEXTURE_ARRAY_LAYERS
                ),
                texture_asset(
                    TERRAIN_ALBEDO_TEXTURE_ARRAY_ID,
                    1,
                    1,
                    REQUIRED_TEXTURE_ARRAY_LAYERS
                ),
                texture_asset(
                    TERRAIN_NORMAL_TEXTURE_ARRAY_ID,
                    1,
                    1,
                    REQUIRED_TEXTURE_ARRAY_LAYERS
                ),
                texture_asset(
                    TERRAIN_MATERIAL_TEXTURE_ARRAY_ID,
                    1,
                    1,
                    REQUIRED_TEXTURE_ARRAY_LAYERS
                ),
            ]),
            Err(TerrainTextureError::DuplicateTextureArray(
                TERRAIN_ALBEDO_TEXTURE_ARRAY_ID.into()
            ))
        );
        assert_eq!(
            TerrainTextureArrays::from_assets(vec![
                texture_asset(
                    TERRAIN_ALBEDO_TEXTURE_ARRAY_ID,
                    1,
                    1,
                    REQUIRED_TEXTURE_ARRAY_LAYERS
                ),
                texture_asset(
                    TERRAIN_NORMAL_TEXTURE_ARRAY_ID,
                    1,
                    1,
                    REQUIRED_TEXTURE_ARRAY_LAYERS
                ),
            ]),
            Err(TerrainTextureError::MissingTextureArray(
                TERRAIN_MATERIAL_TEXTURE_ARRAY_ID
            ))
        );
    }

    #[test]
    fn rejects_invalid_texture_array_shapes_and_data_lengths() {
        assert_eq!(
            TerrainTextureArrays::from_assets(vec![
                texture_asset(
                    TERRAIN_ALBEDO_TEXTURE_ARRAY_ID,
                    2,
                    2,
                    REQUIRED_TEXTURE_ARRAY_LAYERS
                ),
                texture_asset(
                    TERRAIN_NORMAL_TEXTURE_ARRAY_ID,
                    1,
                    2,
                    REQUIRED_TEXTURE_ARRAY_LAYERS
                ),
                texture_asset(
                    TERRAIN_MATERIAL_TEXTURE_ARRAY_ID,
                    2,
                    2,
                    REQUIRED_TEXTURE_ARRAY_LAYERS
                ),
            ]),
            Err(TerrainTextureError::TextureShapeMismatch {
                id: TERRAIN_NORMAL_TEXTURE_ARRAY_ID.into(),
                width: 1,
                height: 2,
                layers: REQUIRED_TEXTURE_ARRAY_LAYERS,
                expected_width: 2,
                expected_height: 2,
                expected_layers: REQUIRED_TEXTURE_ARRAY_LAYERS,
            })
        );

        let mut invalid = texture_asset(
            TERRAIN_ALBEDO_TEXTURE_ARRAY_ID,
            2,
            2,
            REQUIRED_TEXTURE_ARRAY_LAYERS,
        );
        invalid.data.pop();
        assert_eq!(
            TerrainTextureArrays::from_assets(vec![
                invalid,
                texture_asset(
                    TERRAIN_NORMAL_TEXTURE_ARRAY_ID,
                    2,
                    2,
                    REQUIRED_TEXTURE_ARRAY_LAYERS
                ),
                texture_asset(
                    TERRAIN_MATERIAL_TEXTURE_ARRAY_ID,
                    2,
                    2,
                    REQUIRED_TEXTURE_ARRAY_LAYERS
                ),
            ]),
            Err(TerrainTextureError::InvalidTextureDataLength {
                id: TERRAIN_ALBEDO_TEXTURE_ARRAY_ID.into(),
                actual: (2 * 2 * REQUIRED_TEXTURE_ARRAY_LAYERS * 4 - 1) as usize,
                expected: (2 * 2 * REQUIRED_TEXTURE_ARRAY_LAYERS * 4) as usize,
            })
        );

        assert_eq!(
            TerrainTextureArrays::from_assets(vec![
                texture_asset(
                    TERRAIN_ALBEDO_TEXTURE_ARRAY_ID,
                    1,
                    1,
                    REQUIRED_TEXTURE_ARRAY_LAYERS - 1
                ),
                texture_asset(
                    TERRAIN_NORMAL_TEXTURE_ARRAY_ID,
                    1,
                    1,
                    REQUIRED_TEXTURE_ARRAY_LAYERS
                ),
                texture_asset(
                    TERRAIN_MATERIAL_TEXTURE_ARRAY_ID,
                    1,
                    1,
                    REQUIRED_TEXTURE_ARRAY_LAYERS
                ),
            ]),
            Err(TerrainTextureError::InvalidTextureDataLength {
                id: TERRAIN_ALBEDO_TEXTURE_ARRAY_ID.into(),
                actual: ((REQUIRED_TEXTURE_ARRAY_LAYERS - 1) * 4) as usize,
                expected: ((REQUIRED_TEXTURE_ARRAY_LAYERS - 1) * 4) as usize,
            })
        );
    }

    #[test]
    fn formats_texture_errors_for_browser_diagnostics() {
        assert_eq!(
            TerrainTextureError::InvalidManifest("bad json".into()).to_string(),
            "invalid terrain texture manifest: bad json"
        );
        assert_eq!(
            TerrainTextureError::InvalidLayerCount {
                actual: 1,
                expected: 16,
            }
            .to_string(),
            "terrain texture manifest has 1 material layers; expected 16"
        );
        assert_eq!(
            TerrainTextureError::MissingTexturePath {
                material_index: 2,
                map: "albedo",
            }
            .to_string(),
            "terrain texture manifest material 2 is missing a albedo map path"
        );
        assert_eq!(
            TerrainTextureError::UnknownTextureArray("x".into()).to_string(),
            "browser returned unknown texture array 'x'"
        );
        assert_eq!(
            TerrainTextureError::DuplicateTextureArray("x".into()).to_string(),
            "browser returned duplicate texture array 'x'"
        );
        assert_eq!(
            TerrainTextureError::MissingTextureArray("x").to_string(),
            "browser did not return texture array 'x'"
        );
        assert_eq!(
            TerrainTextureError::TextureShapeMismatch {
                id: "x".into(),
                width: 1,
                height: 2,
                layers: 3,
                expected_width: 4,
                expected_height: 5,
                expected_layers: 6,
            }
            .to_string(),
            "texture array 'x' has shape 1x2x3; expected 4x5x6"
        );
        assert_eq!(
            TerrainTextureError::InvalidTextureDataLength {
                id: "x".into(),
                actual: 7,
                expected: 8,
            }
            .to_string(),
            "texture array 'x' has 7 bytes; expected 8"
        );
    }

    fn texture_asset(id: &str, width: u32, height: u32, layers: u32) -> RgbaTextureArrayAsset {
        RgbaTextureArrayAsset {
            id: id.to_string(),
            width,
            height,
            layers,
            data: vec![0; (width * height * layers * 4) as usize],
        }
    }

    fn manifest_json() -> String {
        manifest_json_with_layers(REQUIRED_TEXTURE_ARRAY_LAYERS as usize)
    }

    fn manifest_json_with_layers(layers: usize) -> String {
        let materials = (0..layers)
            .map(|index| material_json(index, None))
            .collect::<Vec<_>>()
            .join(",");
        format!(r#"{{"materials":[{materials}]}}"#)
    }

    fn manifest_json_with_missing_path(index_to_clear: usize, map_to_clear: &str) -> String {
        let materials = (0..REQUIRED_TEXTURE_ARRAY_LAYERS as usize)
            .map(|index| material_json(index, (index == index_to_clear).then_some(map_to_clear)))
            .collect::<Vec<_>>()
            .join(",");
        format!(r#"{{"materials":[{materials}]}}"#)
    }

    fn material_json(index: usize, missing_map: Option<&str>) -> String {
        let albedo = map_path(index, "albedo", missing_map);
        let normal = map_path(index, "normal", missing_map);
        let roughness = map_path(index, "roughness", missing_map);

        format!(
            r#"{{"maps":{{"albedo":{{"path":"{albedo}"}},"normal":{{"path":"{normal}"}},"roughness":{{"path":"{roughness}"}}}}}}"#
        )
    }

    fn map_path(index: usize, map: &str, missing_map: Option<&str>) -> String {
        if missing_map == Some(map) {
            String::new()
        } else {
            format!("textures/{map}-{index}.png")
        }
    }
}
