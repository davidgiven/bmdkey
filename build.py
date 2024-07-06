from build.ab import export, normalrule
from build.c import cxxprogram

cxxprogram(
    name="se-unlock",
    srcs=["src/main.cc"]
)

export(
    name="all", items={"se-unlock": "+se-unlock"}
)
