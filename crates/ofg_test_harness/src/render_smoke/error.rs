// Shared error type for OFG native smoke harness commands.

use std::error::Error;
use std::fmt;

pub type HarnessResult<T> = Result<T, HarnessError>;

#[derive(Debug)]
pub struct HarnessError {
    message: String,
}

/// Builds a harness error from a displayable message.
pub fn harness_error(message: impl Into<String>) -> HarnessError {
    HarnessError {
        message: message.into(),
    }
}

impl fmt::Display for HarnessError {
    /// Formats the harness error message.
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.message)
    }
}

impl Error for HarnessError {}

impl From<std::io::Error> for HarnessError {
    /// Converts filesystem errors into harness errors.
    fn from(error: std::io::Error) -> Self {
        harness_error(error.to_string())
    }
}

impl From<image::ImageError> for HarnessError {
    /// Converts image encoding errors into harness errors.
    fn from(error: image::ImageError) -> Self {
        harness_error(error.to_string())
    }
}

impl From<serde_json::Error> for HarnessError {
    /// Converts JSON encoding errors into harness errors.
    fn from(error: serde_json::Error) -> Self {
        harness_error(error.to_string())
    }
}

impl From<wgpu::RequestDeviceError> for HarnessError {
    /// Converts wgpu device-request errors into harness errors.
    fn from(error: wgpu::RequestDeviceError) -> Self {
        harness_error(error.to_string())
    }
}
