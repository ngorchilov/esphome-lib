import esphome.codegen as cg

CODEOWNERS = ["@ngorchilov"]

tuya_product_ns = cg.esphome_ns.namespace("tuya_product")
TuyaProductComponent = tuya_product_ns.class_("TuyaProductComponent", cg.Component)
