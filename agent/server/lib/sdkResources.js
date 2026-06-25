const familyOrder = [
  'LED',
  'BUTTON',
  'SERVO',
  'I2C',
  'UART',
  'MOTOR',
  'ENCODER'
];

const familyLabels = {
  LED: 'LED',
  BUTTON: 'Button',
  SERVO: 'Servo',
  I2C: 'I2C',
  UART: 'UART',
  MOTOR: 'Motor',
  ENCODER: 'Encoder'
};

const familySdk = {
  LED: 'driver_led',
  BUTTON: 'driver_button',
  SERVO: 'driver_servo',
  I2C: 'bsp_i2c',
  UART: 'bsp_uart',
  SPI: 'bsp_spi',
  CAN: 'bsp_can',
  MOTOR: 'driver_motor',
  ENCODER: 'driver_encoder'
};

function sdkForFamily(family) {
  const normalized = String(family || '').replace(/\d+$/, '');
  return familySdk[family] || familySdk[normalized] || 'board';
}

function resourceFamily(macro) {
  const match = String(macro || '').match(/^BOARD_([A-Z0-9]+)_/);
  return match?.[1] || 'BOARD';
}

function resourceSide(parts) {
  if (parts.includes('LEFT')) return 'L';
  if (parts.includes('RIGHT')) return 'R';
  const numeric = parts.find((part) => /^\d+$/.test(part));
  return numeric || '';
}

function resourceName(family, macros) {
  const label = familyLabels[family] || family;
  const parts = macros.flatMap((macro) => macro.replace(/^BOARD_/, '').split('_'));
  const side = resourceSide(parts);
  return side ? `${label} ${side}` : label;
}

function resourceInterface(family, macros) {
  if (/^I2C\d*$/.test(family)) return 'I2C';
  if (/^SPI\d*$/.test(family)) return 'SPI';
  if (/^UART\d*$/.test(family) || family === 'UART') return 'UART';
  if (/^CAN\d*$/.test(family) || family === 'CAN') return 'CAN';
  if (family === 'ENCODER') return 'PCNT/GPIO';
  if (macros.some((macro) => /PWM|LEDC|CHANNEL|TIMER|DUTY|FREQUENCY/.test(macro))) {
    return macros.some((macro) => /GPIO/.test(macro)) ? 'GPIO/PWM' : 'PWM';
  }
  if (macros.some((macro) => /_I2C_|I2C_/.test(macro))) return 'I2C';
  if (macros.some((macro) => /_SPI_|SPI_/.test(macro))) return 'SPI';
  if (macros.some((macro) => /GPIO|ACTIVE_LEVEL|PULL/.test(macro))) return 'GPIO';
  return 'board';
}

function compactMacroLabel(macro, family) {
  return String(macro || '')
    .replace(/^BOARD_/, '')
    .replace(new RegExp(`^${family}_`), '')
    .replace(/^(LEFT|RIGHT)_/, '')
    .replace(/_0$/, '')
    .replace(/_1$/, '')
    .replace(/^BAUD_RATE$/, 'BAUD')
    .replace(/^FREQUENCY_HZ$/, 'FREQ')
    .replace(/_GPIO$/, '')
    .replace(/_HZ$/, '')
    .replace(/_ADDRESS$/, '_ADDR')
    .replace(/_CHANNEL$/, '_CH')
    .replace(/_FREQUENCY$/, '_FREQ')
    .replace(/_BAUD_RATE$/, '_BAUD')
    .replace(/_ACTIVE_LEVEL$/, '_LEVEL');
}

function pinText(family, defines, macros) {
  const visible = macros.filter((macro) => (
    /GPIO|CHANNEL|ADDRESS|BAUD_RATE|FREQUENCY_HZ|PORT|ACTIVE_LEVEL/.test(macro)
  ));
  const selected = visible.length > 0 ? visible : macros;
  return selected
    .map((macro) => `${compactMacroLabel(macro, family)}=${resolveDefineValue(defines, defines[macro])}`)
    .join(' / ');
}

function sortFamilies(a, b) {
  const ai = familyOrder.indexOf(a);
  const bi = familyOrder.indexOf(b);
  if (ai !== -1 || bi !== -1) {
    return (ai === -1 ? Number.MAX_SAFE_INTEGER : ai) - (bi === -1 ? Number.MAX_SAFE_INTEGER : bi);
  }
  return a.localeCompare(b);
}

function splitResourceGroups(defines) {
  const groups = new Map();
  for (const macro of Object.keys(defines || {}).sort()) {
    if (!macro.startsWith('BOARD_')) continue;
    const family = resourceFamily(macro);
    const parts = macro.replace(/^BOARD_/, '').split('_');
    const side = resourceSide(parts);
    const key = side ? `${family}:${side}` : family;
    if (!groups.has(key)) {
      groups.set(key, { family, macros: [] });
    }
    groups.get(key).macros.push(macro);
  }
  return [...groups.values()].sort((a, b) => {
    const familyDiff = sortFamilies(a.family, b.family);
    if (familyDiff !== 0) return familyDiff;
    return resourceName(a.family, a.macros).localeCompare(resourceName(b.family, b.macros));
  });
}

export function loadBoardResourcesFromDefines(defines = {}) {
  return splitResourceGroups(defines).map((group) => ({
    name: resourceName(group.family, group.macros),
    family: group.family,
    interface: resourceInterface(group.family, group.macros),
    pins: pinText(group.family, defines, group.macros),
    sdk: sdkForFamily(group.family),
    source: 'board_pins.h',
    macros: group.macros.map((macro) => ({ name: macro, value: defines[macro] }))
  }));
}

function resolveDefineValue(defines, value, seen = new Set()) {
  const raw = String(value || '').trim();
  if (!/^[A-Z][A-Z0-9_]+$/.test(raw) || seen.has(raw)) {
    return raw;
  }
  seen.add(raw);
  return Object.prototype.hasOwnProperty.call(defines, raw)
    ? resolveDefineValue(defines, defines[raw], seen)
    : raw;
}

function componentBus(macro) {
  if (/_I2C_/.test(macro)) return 'I2C';
  if (/_SPI_/.test(macro)) return 'SPI';
  if (/_UART_/.test(macro)) return 'UART';
  return 'BUS';
}

export function loadComponentBusResourcesFromDefines(componentName, defines = {}) {
  return Object.keys(defines)
    .filter((macro) => /^DRIVER_[A-Z0-9_]+_/.test(macro))
    .filter((macro) => /(?:_I2C_|_SPI_|_UART_).*(?:ADDR|ADDRESS)|(?:ADDR|ADDRESS).*(?:_I2C_|_SPI_|_UART_)/.test(macro))
    .sort()
    .map((macro) => ({
      component: componentName,
      bus: componentBus(macro),
      name: macro,
      value: defines[macro],
      resolvedValue: resolveDefineValue(defines, defines[macro]),
      source: 'component header'
    }));
}
