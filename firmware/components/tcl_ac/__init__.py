import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]
namespace = cg.esphome_ns.namespace("wl6u_ac")
TclAc = namespace.class_("TclAc", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = cv.Schema({cv.GenerateID(): cv.declare_id(TclAc)}).extend(
    cv.COMPONENT_SCHEMA
).extend(uart.UART_DEVICE_SCHEMA)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "tcl_ac", baud_rate=9600, require_tx=True, require_rx=True,
    data_bits=8, parity="EVEN", stop_bits=1,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
