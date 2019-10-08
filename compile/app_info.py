from env_settings import WIN32_VCVARSALL

PROJECT_NAME = "flatgrass"

DEPLOY_FILES = [
	"data",
	"logs",
	"shaders",
	"flatgrass_win32.exe"
]

LIBS_EXTERNAL = {
	"freetype": {
		"path": "freetype-2.8.1",
		"includeDir": True,
		"compiled": True,
		"compiledName": {
			"debug": "freetype281MTd.lib",
			"release": "freetype281MT.lib"
		}
	},
	"libclang": {
		"path": "libclang-9.0.0",
		"includeDir": True,
		"compiled": True,
		"compiledName": {
			"debug": "libclang.lib",
			"release": "libclang.lib"
		}
	},
	"stbimage": {
		"path": "stb_image-2.23",
		"includeDir": False,
		"compiled": False
	},
	"stbsprintf": {
		"path": "stb_sprintf-1.06",
		"includeDir": False,
		"compiled": False
	}
}

PATHS = {
	"win32-vcvarsall": WIN32_VCVARSALL
}