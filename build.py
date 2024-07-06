from build.ab import export
from build.c import cxxprogram
from build.pkg import package

package(name="hidapi-libusb", package="hidapi-libusb")
package(name="fmt", package="fmt")

cxxprogram(
    name="se-unlock",
    srcs=["src/main.cc"],
    deps=["+hidapi-libusb", "+fmt"],
)

export(
    name="all", items={"se-unlock": "+se-unlock"}
)
