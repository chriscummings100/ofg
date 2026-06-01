#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Vec3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl Vec3 {
    pub const ZERO: Self = Self::new(0.0, 0.0, 0.0);
    pub const ONE: Self = Self::new(1.0, 1.0, 1.0);
    pub const UP: Self = Self::new(0.0, 1.0, 0.0);

    pub const fn new(x: f32, y: f32, z: f32) -> Self {
        Self { x, y, z }
    }

    pub(crate) fn add(self, other: Self) -> Self {
        Self::new(self.x + other.x, self.y + other.y, self.z + other.z)
    }

    pub(crate) fn scale(self, amount: f32) -> Self {
        Self::new(self.x * amount, self.y * amount, self.z * amount)
    }

    pub(crate) fn mul(self, other: Self) -> Self {
        Self::new(self.x * other.x, self.y * other.y, self.z * other.z)
    }

    pub(crate) fn length(self) -> f32 {
        (self.x * self.x + self.y * self.y + self.z * self.z).sqrt()
    }

    pub(crate) fn normalize(self) -> Self {
        let length = self.length();
        if length <= f32::EPSILON {
            return Self::ZERO;
        }

        self.scale(1.0 / length)
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Quat {
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub w: f32,
}

impl Quat {
    pub const IDENTITY: Self = Self::new(0.0, 0.0, 0.0, 1.0);

    pub const fn new(x: f32, y: f32, z: f32, w: f32) -> Self {
        Self { x, y, z, w }
    }

    pub fn from_axis_angle(axis: Vec3, angle_radians: f32) -> Self {
        let half_angle = angle_radians * 0.5;
        let s = half_angle.sin();

        Self::new(axis.x * s, axis.y * s, axis.z * s, half_angle.cos()).normalize()
    }

    pub fn from_yaw(yaw_radians: f32) -> Self {
        Self::from_axis_angle(Vec3::new(0.0, 1.0, 0.0), yaw_radians)
    }

    pub fn from_yaw_pitch(yaw_radians: f32, pitch_radians: f32) -> Self {
        Self::from_yaw(yaw_radians).mul(Self::from_axis_angle(
            Vec3::new(1.0, 0.0, 0.0),
            -pitch_radians,
        ))
    }

    pub fn normalize(self) -> Self {
        let length = (self.x * self.x + self.y * self.y + self.z * self.z + self.w * self.w).sqrt();
        if length <= f32::EPSILON {
            return Self::IDENTITY;
        }

        Self::new(
            self.x / length,
            self.y / length,
            self.z / length,
            self.w / length,
        )
    }

    pub(crate) fn mul(self, other: Self) -> Self {
        Self::new(
            self.w * other.x + self.x * other.w + self.y * other.z - self.z * other.y,
            self.w * other.y - self.x * other.z + self.y * other.w + self.z * other.x,
            self.w * other.z + self.x * other.y - self.y * other.x + self.z * other.w,
            self.w * other.w - self.x * other.x - self.y * other.y - self.z * other.z,
        )
        .normalize()
    }

    pub(crate) fn rotate_vec3(self, value: Vec3) -> Vec3 {
        let q = self.normalize();
        let x2 = q.x + q.x;
        let y2 = q.y + q.y;
        let z2 = q.z + q.z;
        let xx = q.x * x2;
        let yy = q.y * y2;
        let zz = q.z * z2;
        let xy = q.x * y2;
        let xz = q.x * z2;
        let yz = q.y * z2;
        let wx = q.w * x2;
        let wy = q.w * y2;
        let wz = q.w * z2;

        Vec3::new(
            (1.0 - (yy + zz)) * value.x + (xy - wz) * value.y + (xz + wy) * value.z,
            (xy + wz) * value.x + (1.0 - (xx + zz)) * value.y + (yz - wx) * value.z,
            (xz - wy) * value.x + (yz + wx) * value.y + (1.0 - (xx + yy)) * value.z,
        )
    }
}
