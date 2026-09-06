SUMMARY = "NanoKVM OLED service extension"
LICENSE = "GPL-3.0-only & OFL-1.1"
LIC_FILES_CHKSUM = "\
    file://LICENSE;md5=1ebbd3e34237af26da5dc08a4e440464 \
    file://fonts/LICENSE-OFL-1.1.txt;md5=a5e2d4d4ba520789e005e841003bbf1d \
"
PR = "r1"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
ONEKVM_COMPONENT_GIT_URI ??= "git://github.com/onekvm/onekvm-extension-nanokvm-oled.git;protocol=https;branch=main"
ONEKVM_COMPONENT_SRCREV ??= ""
SRC_URI = "${ONEKVM_COMPONENT_GIT_URI};destsuffix=${BP}"
SRC_URI += "file://oled-page.js"
SRCREV = "${ONEKVM_COMPONENT_SRCREV}"
inherit pkgconfig onekvm-extension

ONEKVM_EXTENSION_ID = "nanokvm-oled"
ONEKVM_EXTENSION_API_VERSION = "2"
ONEKVM_EXTENSION_MANIFEST_TEMPLATE = "${S}/manifest.json.in"
COMPATIBLE_MACHINE = "^onekvm-nanokvm$"
PACKAGE_ARCH = "${MACHINE_ARCH}"
DEPENDS += "systemd"
RDEPENDS:${PN} += "onekvm-core onekvm-plugin-manager"

EXTRA_OEMAKE = "\
    -C ${S} \
    BINARY=${B}/bin/onekvm-nanokvm-oled \
    DESTDIR=${D} \
    PREFIX=${ONEKVM_EXTENSION_ROOT} \
    VERSION=${PV} \
    ARCH=${TARGET_ARCH} \
"

do_configure[noexec] = "1"

do_compile:prepend() {
    install -Dm0644 ${UNPACKDIR}/oled-page.js ${S}/web/dist/oled-page.js
}

do_compile() {
    oe_runmake
}

do_install() {
    oe_runmake install
}
