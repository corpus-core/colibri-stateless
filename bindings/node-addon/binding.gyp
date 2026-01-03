{
  "targets": [
    {
      "target_name": "colibri_native",
      "sources": [
        "src/addon.c"
      ],
      "include_dirs": [
        "<(module_root_dir)/../.."
      ],
      "defines": [
        "NAPI_VERSION=8",
        "_GNU_SOURCE=1"
      ],
      "conditions": [
        [
          "OS==\"mac\"",
          {
            "xcode_settings": {
              "OTHER_CFLAGS": [
                "-std=c11"
              ]
            }
          }
        ],
        [
          "OS==\"linux\"",
          {
            "cflags": [
              "-std=c11"
            ]
          }
        ]
      ]
    }
  ]
}


