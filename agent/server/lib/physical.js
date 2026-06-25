export function getPhysicalStatus() {
  return {
    available: false,
    adapter: 'placeholder',
    message: 'physical agent / physical framework is not present in this repository yet.',
    plannedInterface: {
      transport: ['serial', 'tcp', 'udp'],
      capabilities: [
        'device discovery',
        'firmware log capture',
        'test command dispatch',
        'sensor/actuator assertion',
        'vibe-code feature loop'
      ]
    }
  };
}
