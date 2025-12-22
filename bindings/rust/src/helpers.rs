use crate::ffi;

pub fn bytes_to_vec(bytes: ffi::bytes_t) -> Vec<u8> {
    if bytes.data.is_null() || bytes.len == 0 {
        Vec::new()
    } else {
        unsafe {
            std::slice::from_raw_parts(bytes.data, bytes.len as usize).to_vec()
        }
    }
}

pub fn slice_to_bytes(data: &[u8]) -> ffi::bytes_t {
    ffi::bytes_t {
        data: data.as_ptr() as *mut u8,
        len: data.len() as u32,
    }
}