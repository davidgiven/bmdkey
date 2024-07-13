from build.ab import export
from build.c import cxxprogram
from build.pkg import package

package(name="hidapi-libusb", package="hidapi-libusb")
package(name="fmt", package="fmt")
package(name="libfakekey", package="libfakekey")

cxxprogram(
    name="bmdkey",
    srcs=["src/main.cc"],
    deps=["+hidapi-libusb", "+fmt", "+libfakekey"],
)

export(name="all", items={"bmdkey": "+bmdkey"})
