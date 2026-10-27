import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate

from . import TclAc, namespace

DEPENDENCIES = ["tcl_ac"]
CONF_TCL_AC_ID = "tcl_ac_id"

TclAcClimate = namespace.class_("TclAcClimate", climate.Climate, cg.Component)

CONFIG_SCHEMA = (
    climate.climate_schema(TclAcClimate)
    .extend({cv.GenerateID(CONF_TCL_AC_ID): cv.use_id(TclAc)})
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    cg.add(var.set_hub(await cg.get_variable(config[CONF_TCL_AC_ID])))
