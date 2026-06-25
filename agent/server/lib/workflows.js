const sdkBoundary = `只允许向 project/ 下创建或修改应用工程；接入 Physical Agent 时，上位机配置草稿允许输出到 physical_agent_config/config/。不要修改 components/、boards/、tools/、env/、third_party/、examples/。优先复用当前 SDK 的 BSP、driver_* 和 service_* 接口。`;

const skillsetSummary = `
Skillset 1: 产品规划、需求澄清、功能分析、器件选型、硬件资源规划、搭建框图、用户 Q&A。
Skillset 2: 硬件模块家族设计生产、硬件搭建、嵌入式开发、测试调试、physical agent 上位机测试预留、结构装配和整机联调。
Skillset 3: 从框图到电路原理图优化、电路板量产设计、datasheet 关键信息提取、AI 原理图生成接口预留。
`;

function slugifyProjectName(input) {
  const base = String(input || 'robot_agent_app')
    .toLowerCase()
    .replace(/[^a-z0-9_ -]+/g, '')
    .trim()
    .replace(/\s+/g, '_')
    .replace(/-+/g, '_');
  return base || 'robot_agent_app';
}

function componentNames(sdkSummary) {
  return (sdkSummary.components || []).map((component) => component.name);
}

function isCh32Board(boardName = '') {
  return /^ch32/i.test(String(boardName || ''));
}

function availableApiText(sdkSummary) {
  const componentApis = (sdkSummary.components || [])
    .map((component) => {
      const apis = component.api
        .map((api) => {
          const declarations = (api.declarations && api.declarations.length > 0)
            ? `Declarations:\n${api.declarations.join('\n\n')}\n`
            : '';
          const signatures = (api.prototypes && api.prototypes.length > 0)
            ? api.prototypes.join('\n')
            : api.functions.join(', ');
          return `${api.header}:\n${declarations}Functions:\n${signatures}`;
        })
        .join('\n');
      return `${component.name}\n${apis}`;
    })
    .join('\n\n');
  const bspHeaders = (sdkSummary.bsp || []).map((name) => `${name}/include/${name}.h`).join('\n');
  const boardNames = (sdkSummary.boards || []).map((board) => board.name).join(', ') || 'unknown';
  return [
    componentApis,
    'BSP 公共头文件：',
    'bsp_board/include/board.h',
    bspHeaders,
    `可选板卡：${boardNames}`
  ].filter(Boolean).join('\n\n');
}

function detectCapabilities(requirement, sdkSummary) {
  const text = String(requirement || '').toLowerCase();
  const components = new Set();
  const notes = [];

  if (/(^|[^a-z])led([^a-z]|$)|灯|亮度|闪烁|呼吸/.test(text)) {
    components.add('driver_led');
    notes.push('LED 状态、亮度、反馈提示');
  }
  if (/button|按键|按钮|短按|长按/.test(text)) {
    components.add('driver_button');
    notes.push('按键输入和模式切换');
  }
  if (/servo|舵机|关节|云台/.test(text)) {
    components.add('driver_servo');
    notes.push('舵机执行机构');
  }
  if (/oled|display|屏|显示/.test(text)) {
    components.add('driver_oled');
    notes.push('本地状态显示');
  }
  if (/mpu|imu|姿态|陀螺|加速度/.test(text)) {
    components.add('driver_mpu6050');
    notes.push('IMU 姿态/运动感知');
  }
  if (/tof|vl53|distance|测距|避障|距离/.test(text)) {
    components.add('driver_tof2000c_vl53l0x');
    notes.push('ToF 测距/避障');
  }
  if (/tts|语音|播报|喇叭/.test(text)) {
    components.add('driver_tw_tts');
    notes.push('语音播报模块');
  }
  if (/wifi|tcp|udp|网络|上位机|remote|远程/.test(text)) {
    components.add('service_wifi');
    notes.push('WiFi 网络和上位机通信');
  }

  const known = new Set(componentNames(sdkSummary));
  return {
    components: [...components].filter((name) => known.has(name)),
    notes
  };
}

function boardResourceLines(sdkSummary, boardName = '') {
  const board = (sdkSummary.boards || []).find((item) => item.name === boardName)
    || (sdkSummary.boards || [])[0];
  if (!board) {
    return ['当前未扫描到 boards/ 下的板级定义。'];
  }

  return [
    `默认参考板卡：${board.name}`,
    ...((board.resources || []).map((resource) => {
      const values = (resource.macros || [])
        .map((macro) => `${macro.name}=${macro.value}`)
        .join(', ');
      return values ? `${resource.name}: ${values}` : `${resource.name}: 当前板级文件未声明`;
    })),
    ...componentBusResourceLines(sdkSummary)
  ];
}

function componentBusResourceLines(sdkSummary) {
  const rows = (sdkSummary.components || [])
    .flatMap((component) => component.busResources || [])
    .filter((resource) => resource.bus === 'I2C');
  if (rows.length === 0) {
    return [];
  }
  return [
    '组件 I2C 地址:',
    ...rows.map((resource) => `${resource.component}: ${resource.name}=${resource.resolvedValue || resource.value}`)
  ];
}

function selectedBoard(sdkSummary, boardName = '') {
  return (sdkSummary.boards || []).find((item) => item.name === boardName)
    || (sdkSummary.boards || [])[0]
    || null;
}

function componentByName(sdkSummary, name) {
  return (sdkSummary.components || []).find((component) => component.name === name) || null;
}

function expandComponentDeps(names, sdkSummary) {
  const result = new Set();
  const visit = (name) => {
    if (!name || result.has(name)) return;
    result.add(name);
    const component = componentByName(sdkSummary, name);
    for (const dep of component?.requires || []) {
      if (componentByName(sdkSummary, dep)) {
        visit(dep);
      }
    }
  };
  for (const name of names || []) {
    visit(name);
  }
  return [...result];
}

function boardMacroStatus(sdkSummary, boardName = '', componentNamesToCheck = []) {
  const board = selectedBoard(sdkSummary, boardName);
  const defines = board?.defines || {};
  const components = expandComponentDeps(componentNamesToCheck, sdkSummary);
  return components
    .map((name) => {
      const component = componentByName(sdkSummary, name);
      const required = component?.requiredBoardMacros || [];
      const missing = required.filter((macro) => !Object.prototype.hasOwnProperty.call(defines, macro));
      return { component: name, required, missing };
    })
    .filter((item) => item.required.length > 0 || item.missing.length > 0);
}

function boardMacroStatusText(sdkSummary, boardName = '', componentNamesToCheck = []) {
  const rows = boardMacroStatus(sdkSummary, boardName, componentNamesToCheck);
  if (rows.length === 0) {
    return '未命中需要 BOARD_* 宏的组件。';
  }
  return rows.map((row) => {
    const state = row.missing.length === 0 ? 'OK' : `缺失: ${row.missing.join(', ')}`;
    return `${row.component}: ${state}`;
  }).join('\n');
}

function buildMermaid(capabilities) {
  const nodes = [
    '用户需求',
    'Moce Hardware Agent',
    '功能分析',
    '器件选型',
    '硬件资源规划',
    'ESP32 SDK 应用'
  ];
  if (capabilities.components.includes('service_wifi')) {
    nodes.push('上位机/physical agent 接口');
  }
  if (capabilities.components.some((name) => name.startsWith('driver_'))) {
    nodes.push('硬件模块与传感器');
  }

  return [
    'flowchart LR',
    '  A[用户需求] --> B[Moce Hardware Agent]',
    '  B --> C[功能分析]',
    '  C --> D[器件选型]',
    '  D --> E[硬件资源规划]',
    '  E --> F[ESP32 SDK 应用]',
    capabilities.components.includes('service_wifi') ? '  F <--> G[上位机 / physical agent 接口]' : '',
    capabilities.components.some((name) => name.startsWith('driver_')) ? '  F --> H[硬件模块与传感器]' : ''
  ].filter(Boolean).join('\n');
}

export function normalizeAgentText(text) {
  return String(text || '').trim();
}

export function parseGeneratedFiles(text) {
  const raw = String(text || '');
  const files = [];
  const fileMarker = /^===== FILE:\s*((?:project|physical_agent_config\/config)\/[^\r\n=]+?)\s*=====\s*$/gm;
  const matches = [...raw.matchAll(fileMarker)];

  for (let index = 0; index < matches.length; index += 1) {
    const current = matches[index];
    const next = matches[index + 1];
    const pathText = current[1].trim();
    const start = current.index + current[0].length;
    const end = next ? next.index : raw.length;
    let content = raw.slice(start, end).trim();
    const fenced = content.match(/^```[a-zA-Z0-9_-]*\s*([\s\S]*?)```$/);
    if (fenced) {
      content = fenced[1].trim();
    }
    if (pathText && content) {
      files.push({ path: pathText, content });
    }
  }

  return files;
}

function selectedComponentText(capabilities) {
  if (capabilities.components.length === 0) {
    return '未命中具体组件，请根据需求继续判断。';
  }
  return capabilities.components.join(', ');
}

function mentionedSdkComponents(text) {
  return [...new Set(String(text || '').match(/\b(?:driver|service)_[a-z0-9_]+\b/g) || [])];
}

export function buildHardwareBuildPrompt({ requirement, sdkSummary, boardName, analysis = '', componentSelection = '', resourcePlan = '' }) {
  return {
    messages: [
      {
        role: 'system',
        content: [
          `你是 Moce SDK 的硬件搭建 agent。${sdkBoundary}`,
          skillsetSummary,
          '本阶段只做硬件搭建：根据已接受的硬件资源规划、器件接口和目标板卡资源，生成模块连接框图、接线清单和上电检查。',
          '不要重新做完整开发规划，不要覆盖或重新分配硬件资源规划中的引脚、接口和地址。',
          'Mermaid 框图必须体现 ESP32/目标板、模块、接口类型和关键引脚/地址；连线标注必须简短，只写 SDA=47、SCL=21、TX=38、RX=39、A=0x3C、PWM=2/CH0 这类信息，不要在框图中写 BOARD_* 宏全称。',
          '框图只绘制信号连接。电源与共地使用一个独立说明节点汇总为“供电 / 共地：统一连接，详见接线清单”，不要从电源或 GND 节点向每个模块扇出连线。',
          '如果模块接口或电源参数不确定，必须标为待确认。'
        ].join('\n')
      },
      {
        role: 'user',
        content: [
          `目标板卡：${boardName || '未指定'}`,
          `用户需求：\n${requirement || '(空)'}`,
          `当前 SDK 组件：${componentNames(sdkSummary).join(', ')}`,
          `BSP：${(sdkSummary.bsp || []).join(', ')}`,
          `Boards：${(sdkSummary.boards || []).map((board) => board.name).join(', ')}`,
          `Examples：${(sdkSummary.examples || []).join(', ')}`,
          `目标板卡资源：\n${boardResourceLines(sdkSummary, boardName).join('\n')}`,
          analysis ? `已接受的功能分析：\n${analysis}` : '已接受的功能分析：暂无，请先按需求分析和硬件功能拆解 skill 补齐。',
          componentSelection ? `已接受的器件选型：\n${componentSelection}` : '已接受的器件选型：暂无，请先完成器件选型。',
          resourcePlan ? `已接受的硬件资源规划：\n${resourcePlan}` : '已接受的硬件资源规划：暂无，请先完成硬件资源规划。',
          [
            '请只输出硬件搭建文档，格式如下：',
            '## 硬件搭建摘要',
            '## 模块连接框图',
            '```mermaid',
            'flowchart LR',
            '  MCU[目标板/ESP32] -- "I2C SDA=47 SCL=21 A=0x3C" --> MODULE[模块]',
            '  POWER["供电 / 共地：统一连接，详见接线清单"]',
            '```',
            '## 模块接线清单',
            '| 模块 | 模块接口/管脚 | 目标板接口/宏 | GPIO/通道/地址 | 供电 | 共地 | 状态 | 备注 |',
            '## 接口分组',
            '| 接口/总线 | 连接模块 | 引脚/端口 | 地址/参数 | 注意事项 |',
            '## 电源和共地',
            '| 模块 | 供电电压 | 峰值电流/功耗 | 供电来源 | 共地要求 | 风险 |',
            '## 上电前检查',
            '## 单模块验证顺序',
            '## 待确认问题',
            '',
            '状态只能使用：可接线、共享可用、待确认、冲突、缺口。'
          ].join('\n')
        ].join('\n\n')
      }
    ]
  };
}

function sdkResourceToolLines(sdkResources) {
  if (!sdkResources) {
    return ['sdkResourceLoader: 未调用'];
  }
  const boardLines = (sdkResources.boardResources || []).map((resource) => (
    `${resource.name}: ${resource.interface}; ${resource.pins}; SDK=${resource.sdk}; source=${resource.source}`
  ));
  const busLines = (sdkResources.componentBusResources || [])
    .filter((resource) => resource.bus === 'I2C')
    .map((resource) => `${resource.component}: ${resource.name}=${resource.resolvedValue || resource.value}`);
  return [
    `sdkResourceLoader: 已调用，board=${sdkResources.board}`,
    '板级资源:',
    ...(boardLines.length ? boardLines : ['(无)']),
    '组件总线地址:',
    ...(busLines.length ? busLines : ['(无)'])
  ];
}

export function buildHardwareResourcePlanningPrompt({ requirement, analysis, componentSelection, resourceNotes = '', sdkSummary, boardName, sdkResources = null }) {
  return {
    messages: [
      {
        role: 'system',
        content: [
          '你是 Moce SDK 的硬件资源规划 agent。',
          sdkBoundary,
          '本阶段只做目标板卡上的接口、引脚和资源分配，不生成固件代码。',
          '必须基于已接受的器件选型，为每个器件或模块分配板级资源。',
          '必须使用 sdkResourceLoader 工具输出的板级资源和组件总线地址作为资源分配依据。',
          '必须使用目标板卡已有 BOARD_* 宏和已扫描到的 board 定义，不要猜 GPIO。',
          '必须检查 GPIO、I2C 地址、UART、PWM/LEDC、PCNT/编码器等资源冲突。',
          '如果资源不足或不确定，必须明确标为缺口或待确认。'
        ].join('\n')
      },
      {
        role: 'user',
        content: [
          `目标板卡：${boardName || '未指定'}`,
          `用户需求：\n${requirement || '(空)'}`,
          `已接受的功能分析：\n${analysis || '(空)'}`,
          `已接受的器件选型：\n${componentSelection || '(空)'}`,
          resourceNotes ? `用户对资源规划的补充/调整要求：\n${resourceNotes}` : '用户对资源规划的补充/调整要求：暂无',
          `当前 SDK 组件：${componentNames(sdkSummary).join(', ')}`,
          `BSP：${(sdkSummary.bsp || []).join(', ')}`,
          `Boards：${(sdkSummary.boards || []).map((board) => board.name).join(', ')}`,
          `Examples：${(sdkSummary.examples || []).join(', ')}`,
          `目标板卡资源：\n${boardResourceLines(sdkSummary, boardName).join('\n')}`,
          `sdkResourceLoader 工具输出：\n${sdkResourceToolLines(sdkResources).join('\n')}`,
          [
            '请只输出硬件资源规划，格式如下：',
            '## 硬件资源规划摘要',
            '## 接口和引脚分配',
            '| 功能/器件 | SDK 组件 | 接口 | 板级资源/宏 | GPIO/通道/地址 | 共享/独占 | 状态 | 备注 |',
            '## 共享总线规划',
            '| 总线 | 引脚/端口 | 挂载器件 | 地址/参数 | 冲突检查 | 备注 |',
            '## 独占资源规划',
            '| 资源类型 | 资源 | 使用者 | 参数 | 状态 | 备注 |',
            '## 冲突检查',
            '| 检查项 | 结果 | 说明 | 处理建议 |',
            '## 接线和初始化建议',
            '| 顺序 | 模块 | 接线/初始化动作 | 依赖 | 验证方式 |',
            '## 待确认问题',
            '## 下一步建议',
            '',
            '状态只能使用：已分配、共享可用、冲突、待确认、缺口。'
          ].join('\n')
        ].join('\n\n')
      }
    ]
  };
}

export function buildComponentSelectionPrompt({ requirement, analysis, selectionNotes = '', sdkSummary, boardName }) {
  return {
    messages: [
      {
        role: 'system',
        content: [
          '你是 Moce SDK 的器件选型 agent。',
          sdkBoundary,
          '本阶段只做器件或模块选型，不生成固件代码。',
          '必须基于已接受的功能分析，为每个硬件功能推荐器件或模块。',
          '必须输出关键参数、接口资源、SDK 适配状态、候选比较、风险和待确认项。',
          '如果资料不足或参数不确定，必须明确标为待确认，不要臆造。'
        ].join('\n')
      },
      {
        role: 'user',
        content: [
          `目标板卡：${boardName || '未指定'}`,
          `用户需求：\n${requirement || '(空)'}`,
          `已接受的功能分析：\n${analysis || '(空)'}`,
          selectionNotes ? `用户对选型的补充/调整要求：\n${selectionNotes}` : '用户对选型的补充/调整要求：暂无',
          `当前 SDK 组件：${componentNames(sdkSummary).join(', ')}`,
          `BSP：${(sdkSummary.bsp || []).join(', ')}`,
          `Boards：${(sdkSummary.boards || []).map((board) => board.name).join(', ')}`,
          `Examples：${(sdkSummary.examples || []).join(', ')}`,
          `目标板卡资源：\n${boardResourceLines(sdkSummary, boardName).join('\n')}`,
          [
            '请只输出器件选型，格式如下：',
            '## 器件选型摘要',
            '## 功能到器件映射',
            '| 功能 | 推荐器件/模块 | 类型 | 接口 | 供电/逻辑电平 | SDK 组件 | 板级资源 | 推荐等级 | 备注 |',
            '## 候选器件比较',
            '| 功能 | 候选项 | 关键参数 | 优点 | 缺点/风险 | 资料来源 | 结论 |',
            '## 关键参数清单',
            '| 器件/模块 | 供电 | 接口 | 地址/通道 | 性能参数 | 电流/功耗 | 尺寸/结构 | 待确认 |',
            '## SDK 适配和资源占用',
            '| 器件/模块 | SDK 支持状态 | 推荐组件/API | 需要资源 | 示例工程 | 适配工作量 | 风险 |',
            '## 不推荐或淘汰项',
            '## 待确认问题',
            '## 下一步建议',
            '',
            '推荐等级只能使用：推荐、可选、待确认、不推荐。'
          ].join('\n')
        ].join('\n\n')
      }
    ]
  };
}

export function buildRequirementAnalysisPrompt({ requirement, sdkSummary, boardName }) {
  const capabilities = detectCapabilities(requirement, sdkSummary);
  return {
    messages: [
      {
        role: 'system',
        content: [
          '你是 Moce SDK 的需求分析和硬件功能拆解 agent。',
          sdkBoundary,
          '本阶段只做功能分析，不生成固件代码，不进入完整开发规划。',
          '必须把自然语言需求拆成需要硬件实现的功能，并映射到当前 SDK 能力。',
          '用户需求中出现的每个硬件模块都必须逐项覆盖；如果当前 SDK 不支持，必须明确标为缺口或待确认。',
          '硬件资源必须以目标板卡资源为准，不要猜 GPIO。',
          '必须判断用户需求是否隐含状态机需求。若存在模式切换、事件响应、持续控制、闭环控制、故障恢复或上位机动作执行，必须设计状态机和状态转移条件。',
          '需要状态机时，必须输出 Mermaid stateDiagram-v2；不需要时写明“暂无状态机图”。'
        ].join('\n')
      },
      {
        role: 'user',
        content: [
          `目标板卡：${boardName || '未指定'}`,
          `用户需求：\n${requirement || '(空)'}`,
          `根据需求初步命中的 SDK 组件：${selectedComponentText(capabilities)}`,
          `当前 SDK 组件：${componentNames(sdkSummary).join(', ')}`,
          `BSP：${(sdkSummary.bsp || []).join(', ')}`,
          `Boards：${(sdkSummary.boards || []).map((board) => board.name).join(', ')}`,
          `Examples：${(sdkSummary.examples || []).join(', ')}`,
          `目标板卡资源：\n${boardResourceLines(sdkSummary, boardName).join('\n')}`,
          [
            '请只输出功能分析，格式如下：',
            '## 需求摘要',
            '## 硬件功能清单',
            '| 功能 | 类型 | 用户行为/系统行为 | SDK 组件 | 板级资源 | 状态 | 备注 |',
            '## SDK 能力映射',
            '| 需求项 | 推荐组件/API | 示例或参考 | 支持状态 | 风险 |',
            '## 状态机需求判断',
            '- 判断：是/否',
            '- 原因：',
            '## 状态机设计',
            '| 状态 | 职责 | 进入条件 | 退出条件 | 硬件动作 |',
            '## 状态转移条件',
            '| From | To | 条件/事件 | 动作 |',
            '## 状态机状态转移图',
            '如果需要状态机，输出 ```mermaid fenced block，内容使用 stateDiagram-v2；否则写“暂无状态机图”。',
            '## 待确认问题',
            '## 缺口和风险',
            '## 建议的下一步',
            '',
            '状态只能使用：已支持、部分支持、缺口、待确认。'
          ].join('\n')
        ].join('\n\n')
      }
    ]
  };
}

export function buildChatPrompt({ question, requirement, currentPlan, sdkSummary }) {
  return {
    messages: [
      {
        role: 'system',
        content: `你是 Moce SDK 的硬件产品开发问答 agent。${sdkBoundary}`
      },
      {
        role: 'user',
        content: [
          `用户需求：${requirement || '(未填写)'}`,
          `当前硬件上下文：${currentPlan || '(暂无)'}`,
          `SDK 组件：${componentNames(sdkSummary).join(', ')}`,
          `问题：${question || '(空)'}`
        ].join('\n\n')
      }
    ]
  };
}

function buildCh32CodegenPrompt({ requirement, projectName, plan, analysis = '', sdkSummary, globalPrompt = '', boardName = '', projectFiles = [] }) {
  const globalConstraint = String(globalPrompt || '').trim();
  const name = slugifyProjectName(projectName || 'oled_096_readout');
  const projectText = (projectFiles || [])
    .filter((file) => file?.path?.startsWith('project/') && typeof file.content === 'string')
    .map((file) => `===== CURRENT FILE: ${file.path} =====\n${file.content}`)
    .join('\n\n');
  return {
    messages: [
      {
        role: 'system',
        content: [
          'You are a Moce SDK CH32 firmware generation agent.',
          sdkBoundary,
          globalConstraint ? `Global SDK constraints from ./prompt/prompt0.md:\n${globalConstraint}` : '',
          'Target SDK is a bare-metal CMake CH32 project, not ESP-IDF.',
          'Generate only files under project/<app>/.',
          'Required output files are project/<app>/CMakeLists.txt and project/<app>/main.c.',
          'Do not generate project/<app>/main/CMakeLists.txt. Do not use idf_component_register, ESP_LOG, FreeRTOS, app_main, ESP-IDF headers, or ESP-IDF dependencies.',
          'Use void app_setup(void) as the application entry point; vibe_runtime calls it.',
          'For CH32 board resources, include "board_pins.h" when using BOARD_* pin macros.',
          'Prefer existing public APIs: "vibe_api.h", "vibe_runtime.h", "mcu_port_i2c.h", "mcu_port_time.h", and scanned component headers.',
          'For a 0.96 OLED over I2C, prefer the style used by examples/oled_096_readout: mcu_port_i2c_t, BOARD_I2C1_* pins, OLED 8-bit address 0x7A / 7-bit 0x3D, oled_write/oled_cmd/oled_data helpers, and vibe_task_every_ms for periodic refresh.',
          'CMakeLists.txt must follow the existing examples: add_executable(${APP}.elf main.c), link board_${BOARD} and user_api, apply ${COMMON_LINK_FLAGS}, and generate .hex/.bin with CMAKE_OBJCOPY.',
          'project/<app>/CMakeLists.txt is included by the SDK root CMakeLists.txt through add_subdirectory(project/${APP}); it must not call cmake_minimum_required(), project(), set(APP ...), set(BOARD ...), or include(...).',
          'Output format must be exact file blocks and nothing else:',
          '===== FILE: project/<app>/CMakeLists.txt =====',
          '<complete file content>',
          '===== FILE: project/<app>/main.c =====',
          '<complete file content>'
        ].filter(Boolean).join('\n')
      },
      {
        role: 'user',
        content: [
          `Project: project/${name}/`,
          `Target board: ${boardName || 'ch32v203g6u6'}`,
          `Requirement:\n${requirement || '(not specified)'}`,
          `Accepted analysis:\n${analysis || '(none)'}`,
          `Hardware build plan:\n${plan || '(none)'}`,
          `Current generated project files:\n${projectText || '(none)'}`,
          `Board resources:\n${boardResourceLines(sdkSummary, boardName).join('\n')}`,
          `Available API:\n${availableApiText(sdkSummary)}`
        ].join('\n\n')
      }
    ]
  };
}

export function buildCodegenPrompt({ requirement, projectName, plan, analysis = '', sdkSummary, globalPrompt = '', boardName = '', projectFiles = [] }) {
  if (isCh32Board(boardName)) {
    return buildCh32CodegenPrompt({ requirement, projectName, plan, analysis, sdkSummary, globalPrompt, boardName, projectFiles });
  }

  const globalConstraint = String(globalPrompt || '').trim();
  const capabilities = detectCapabilities(`${requirement}\n${analysis}\n${plan}`, sdkSummary);
  const availableComponents = new Set(componentNames(sdkSummary));
  const requestedComponents = [...new Set([
    ...capabilities.components,
    ...mentionedSdkComponents(`${requirement}\n${analysis}\n${plan}`)
  ])];
  const matchedComponents = requestedComponents.filter((name) => availableComponents.has(name));
  const missingComponents = requestedComponents.filter((name) => !availableComponents.has(name));
  const projectText = (projectFiles || [])
    .filter((file) => file?.path?.startsWith('project/') && typeof file.content === 'string')
    .map((file) => `===== CURRENT FILE: ${file.path} =====\n${file.content}`)
    .join('\n\n');
  return {
    messages: [
      {
        role: 'system',
        content: [
          '你是 Moce SDK 的嵌入式应用代码生成 agent。',
          sdkBoundary,
          globalConstraint
            ? `以下是 ./prompt/prompt0.md，本轮生成固件草稿必须逐条遵守，优先级高于用户需求中的冲突内容：\n${globalConstraint}`
            : '本轮生成固件草稿必须遵守 ./prompt/prompt0.md 的全局开发约束；如果文件缺失，也必须只输出 project/ 下应用工程。',
          '生成完整可编译 ESP-IDF 应用工程。必须包含 project/<name>/CMakeLists.txt、project/<name>/main/CMakeLists.txt、project/<name>/main/main.c。',
          '根 CMakeLists.txt 必须是 ESP-IDF 工程入口：包含 cmake_minimum_required(VERSION 3.16)、EXTRA_COMPONENT_DIRS、include($ENV{IDF_PATH}/tools/cmake/project.cmake) 和 project(<name>)。',
          '根 CMakeLists.txt 禁止出现 idf_component_register、register_component、SRCS、REQUIRES 等组件级写法。',
          'main/CMakeLists.txt 必须使用 idf_component_register(SRCS "main.c" REQUIRES ...)，禁止使用 register_component()、COMPONENT_SRCS、COMPONENT_REQUIRES 等旧式写法。',
          'main/CMakeLists.txt 的 REQUIRES 只能包含当前 SDK 扫描到的 components/BSP 组件名和确实需要的 ESP-IDF 组件；不要因为 include 了 ESP-IDF/FreeRTOS 标准头就加入 esp_log、esp_err、sdkconfig、FreeRTOS、task 等伪组件。',
          '包含 "esp_log.h" 不需要也不允许写 REQUIRES esp_log；包含 "freertos/FreeRTOS.h" 或 "freertos/task.h" 不要写 REQUIRES FreeRTOS 或 task。',
          '必须严格使用下方提供的 C 函数原型、结构体字段、类型名和枚举名；不要改名，不要臆造 wrapper，不要把 void 返回值当成 esp_err_t。',
          '生成代码前必须先对照当前 SDK 扫描摘要检查 components/ 是否存在相应组件；只允许引用已扫描到的组件头文件和函数。',
          '如果某个需求模块在 components/ 中没有对应组件，只能生成 TODO、日志或最小可编译占位，不要 include 或调用不存在的 driver/service。',
          '生成 include、函数调用和 main/CMakeLists.txt REQUIRES 前，必须检查目标板卡 board.h 是否定义该组件及其传递依赖所需的全部 BOARD_* 宏；缺任何宏时禁止使用该组件，只能 TODO/降级。',
          '组件存在不代表当前板卡可用；例如 driver_motor 会传递依赖 driver_tb6612 和 driver_encoder，必须同时具备 BOARD_MOTOR_* 与 BOARD_ENCODER_* 宏。',
          '如果声明 static const char *TAG 或 #define TAG，必须至少使用一次 ESP_LOGI/W/E/D(TAG, ...)；如果不需要日志，就不要声明 TAG 或 include esp_log.h。',
          '使用 snprintf 写入固定长度显示行、小数组或 OLED 文本缓冲区时，来自数组/状态字段的 %s 必须带精度限制，例如 %.19s；不要把长数组字符串无界写入更小数组。',
          '配置结构体字段必须完全匹配可用 API 里的头文件定义；例如 bsp_uart_config_t 使用 tx_gpio/rx_gpio，禁止写 tx_pin/rx_pin。',
          '不要留下未调用的 static helper 函数、未使用变量或未使用全局状态；预留逻辑只写 TODO，不生成死代码。',
          '如果已接受的功能分析包含状态机，必须按其中的状态机设计、状态转移条件和完整性检查生成业务逻辑；不要另起一套状态机。',
          '状态机代码必须包含清晰的状态表示、状态转移判断和进入状态时的一次性副作用，避免重复播报、重复刷新、无条件震荡和遗漏异常路径。',
          '优先调用 components/ 中已有 driver_* 和 service_* 的公共函数；不要在应用层绕过组件直接访问 I2C/UART/PWM/GPIO 等底层 BSP，除非没有上层组件且可用 API 明确允许。',
          '只能 #include 下方“可用 API”和“BSP 公共头文件”中真实存在的头文件，或者 ESP-IDF 标准头。板级宏头文件名是 "board.h"，不是 "bsp_board.h"。',
          '如果 main.c 调用了某个组件函数，必须 #include 对应公共头，并在 project/<name>/main/CMakeLists.txt 的 REQUIRES 中加入对应组件名。',
          '不要为了使用板级默认引脚而在应用层臆造 BSP 头文件；优先调用 driver_*_init_default() 或已提供的默认配置函数。',
          '输出格式必须严格使用文件块，不要输出解释文字：',
          '===== FILE: project/<name>/CMakeLists.txt =====',
          '<完整文件内容>',
          '===== FILE: project/<name>/main/CMakeLists.txt =====',
          '<完整文件内容>',
          '===== FILE: project/<name>/main/main.c =====',
          '<完整文件内容>',
          '固件工程文件只输出 project/ 下文件；接入 Physical Agent 时，可额外输出 physical_agent_config/config/ 下的上位机配置草稿。'
        ].join('\n')
      },
      {
        role: 'user',
        content: [
          `工程名：project/${slugifyProjectName(projectName || 'robot_agent_app')}/`,
          `用户需求：${requirement || '(未填写)'}`,
          `已接受的功能分析（必须按其中的状态机设计和状态转移条件实现业务逻辑）：\n${analysis || '(暂无)'}`,
          `当前硬件搭建文档：${plan || '(暂无)'}`,
          `当前已生成固件文件（如果用户给出了实现情况反馈，应在这些文件基础上调整；仍必须输出完整 project/ 文件块）：\n${projectText || '(无)'}`,
          `根据需求和硬件搭建文档初步命中的组件：${matchedComponents.join(', ') || '(未命中)'}`,
          `缺失组件（禁止 include 或调用，只能 TODO/降级）：${missingComponents.join(', ') || '(无)'}`,
          `当前 components/ 扫描结果：${componentNames(sdkSummary).join(', ') || '(无)'}`,
          `目标板卡：${boardName || '未指定'}`,
          `目标板卡 BOARD_* 宏支持检查：\n${boardMacroStatusText(sdkSummary, boardName, matchedComponents)}`,
          `可用 API：\n${availableApiText(sdkSummary)}`
        ].join('\n\n')
      }
    ]
  };
}

export function buildDebugFixPrompt({ requirement, projectName, plan, analysis = '', sdkSummary, toolContext, projectFiles, globalPrompt = '', boardName = '' }) {
  const globalConstraint = String(globalPrompt || '').trim();
  const projectText = (projectFiles || [])
    .map((file) => `===== CURRENT FILE: ${file.path} =====\n${file.content}`)
    .join('\n\n');
  return {
    messages: [
      {
        role: 'system',
        content: [
          '你是 Moce SDK 的测试调试自动修复 agent。',
          sdkBoundary,
          globalConstraint
            ? `以下是 ./prompt/prompt0.md，本轮调试修复必须逐条遵守：\n${globalConstraint}`
            : '本轮调试修复必须遵守 ./prompt/prompt0.md 的全局开发约束。',
          '目标：根据 Build/Flash/Monitor 的关键错误，最小化修复 project/<name>/ 下的应用工程。',
          '只允许输出需要更新的 project/ 文件块。不要输出解释文字，不要输出 SDK、BSP、components、boards、tools 下文件。',
          '优先修改 project/<name>/main/main.c、project/<name>/main/CMakeLists.txt、project/<name>/CMakeLists.txt。',
          '根 CMakeLists.txt 必须是工程入口，包含 cmake_minimum_required(VERSION 3.16)、EXTRA_COMPONENT_DIRS、include(project.cmake) 和 project(<name>)；组件依赖只写在 main/CMakeLists.txt。',
          '只能 #include 下方“可用 API”和“BSP 公共头文件”中真实存在的头文件，或者 ESP-IDF 标准头。板级宏头文件名是 "board.h"，不是 "bsp_board.h"。',
          '如果 main.c 调用了某个组件函数，必须 #include 对应公共头，并在 main/CMakeLists.txt 的 REQUIRES 中加入对应组件名。',
          '不要因为 include 了 "esp_log.h"、"freertos/FreeRTOS.h"、"freertos/task.h" 等标准头就把 esp_log、FreeRTOS、task 写进 REQUIRES。',
          '如果 Build 报 components/ 内缺少 BOARD_* 宏，说明当前板卡不支持该组件或其传递依赖；不要修改 components/，应从 project/ 中移除该组件 include/调用/REQUIRES，或降级为 TODO。',
          '修复前必须检查目标板卡 board.h 是否定义组件及其传递依赖所需的 BOARD_* 宏；缺宏时禁止继续使用该组件。',
          '如果声明 static const char *TAG 或 #define TAG，必须至少使用一次 ESP_LOGI/W/E/D(TAG, ...)；修复 -Wunused-variable 时优先添加有意义日志或删除 TAG/esp_log.h。',
          '修复 format-truncation 时，不要关闭 -Werror；应扩大目标缓冲区，或给 snprintf 的 %s 加精度限制，例如 %.19s。',
          '修复结构体字段错误时，必须对照可用 API/头文件里的字段名；bsp_uart_config_t 是 tx_gpio/rx_gpio，不是 tx_pin/rx_pin。',
          '如果当前 main.c 已实现功能分析中的状态机，修复时必须保持状态、转移条件、事件优先级和进入/退出副作用语义不变；除非错误本身来自状态机实现，否则不要简化或删除状态机。',
          '不要为了绕过编译而删除核心需求逻辑；如果必须降级，只做最小可编译降级并保留 TODO 注释。',
          '输出格式必须严格使用文件块：',
          '===== FILE: project/<name>/path =====',
          '<完整文件内容>'
        ].join('\n')
      },
      {
        role: 'user',
        content: [
          `工程名：project/${slugifyProjectName(projectName || 'robot_agent_app')}/`,
          `用户需求：${requirement || '(未填写)'}`,
          `已接受的功能分析（修复时必须保持其中的状态机语义）：\n${analysis || '(暂无)'}`,
          `当前硬件搭建文档：${plan || '(暂无)'}`,
          `工具错误上下文：\n${toolContext || '(无)'}`,
          `当前 project 文件：\n${projectText || '(无)'}`,
          `目标板卡：${boardName || '未指定'}`,
          `当前 project 组件的目标板卡 BOARD_* 宏支持检查：\n${boardMacroStatusText(sdkSummary, boardName, mentionedSdkComponents(projectText))}`,
          `可用 API：\n${availableApiText(sdkSummary)}`
        ].join('\n\n')
      }
    ]
  };
}

function componentFunctionType(component) {
  if (component === 'driver_button' || component === 'driver_mpu6050' || component === 'driver_tof2000c_vl53l0x') {
    return '输入';
  }
  if (component === 'driver_led' || component === 'driver_oled' || component === 'driver_servo' || component === 'driver_tw_tts') {
    return '输出';
  }
  if (component === 'service_wifi') {
    return '通信';
  }
  return '待确认';
}

function componentResourceText(component, sdkSummary) {
  const resources = boardResourceLines(sdkSummary).join('；');
  if (component === 'driver_led') return resources.match(/LED:[^；]+/)?.[0] || 'LED 板级资源待确认';
  if (component === 'driver_button') return resources.match(/Button:[^；]+/)?.[0] || 'Button 板级资源待确认';
  if (component === 'driver_servo') return resources.match(/Servo:[^；]+/)?.[0] || 'Servo PWM 资源待确认';
  if (component === 'driver_oled' || component === 'driver_mpu6050' || component === 'driver_tof2000c_vl53l0x') {
    return resources.match(/I2C:[^；]+/)?.[0] || 'I2C 资源待确认';
  }
  if (component === 'driver_tw_tts') return resources.match(/UART:[^；]+/)?.[0] || 'UART 资源待确认';
  if (component === 'driver_motor' || component === 'driver_tb6612') return resources.match(/Motor:[^；]+/)?.[0] || 'Motor GPIO/PWM 资源待确认';
  if (component === 'driver_encoder') return resources.match(/Encoder:[^；]+/)?.[0] || 'Encoder GPIO 资源待确认';
  if (component === 'service_wifi') return 'WiFi 射频和网络配置';
  return '待确认';
}

function componentPrimaryApi(component, sdkSummary) {
  const item = (sdkSummary.components || []).find((candidate) => candidate.name === component);
  const api = item?.api?.[0];
  if (!api) return '待确认';
  const prototypes = api.prototypes || [];
  return prototypes.slice(0, 3).join('<br>') || (api.functions || []).slice(0, 4).join(', ');
}

function componentCandidate(component) {
  const candidates = {
    driver_led: '板载 LED / 外接单色 LED',
    driver_button: '板载 BOOT 按键 / 独立轻触开关',
    driver_oled: 'SSD1306 0.96 I2C OLED 模块',
    driver_servo: 'SG90 / MG90S PWM 舵机',
    driver_mpu6050: 'MPU6050 I2C IMU 模块',
    driver_tof2000c_vl53l0x: 'VL53L0X ToF 测距模块',
    driver_tw_tts: 'TW TTS UART 语音模块',
    service_wifi: 'ESP32 内置 WiFi',
    driver_motor: '直流减速电机 + 编码器',
    driver_encoder: 'AB 相增量式编码器',
    driver_tb6612: 'TB6612FNG 双路电机驱动模块',
    service_pid: '软件 PID 控制服务'
  };
  return candidates[component] || `${component} 对应模块待确认`;
}

function componentInterface(component) {
  if (component === 'driver_button') return 'GPIO';
  if (component === 'driver_led' || component === 'driver_servo' || component === 'driver_motor' || component === 'driver_tb6612') return 'GPIO/PWM';
  if (component === 'driver_oled' || component === 'driver_mpu6050' || component === 'driver_tof2000c_vl53l0x') return 'I2C';
  if (component === 'driver_tw_tts') return 'UART';
  if (component === 'service_wifi') return 'WiFi';
  if (component === 'driver_encoder') return 'GPIO/PCNT';
  return '待确认';
}

function componentSupply(component) {
  if (component === 'driver_led' || component === 'driver_button') return '3.3V 逻辑，按板级电路';
  if (component === 'driver_oled' || component === 'driver_mpu6050' || component === 'driver_tof2000c_vl53l0x') return '3.3V/5V 模块待确认，I2C 逻辑需兼容 3.3V';
  if (component === 'driver_servo' || component === 'driver_motor' || component === 'driver_tb6612') return '独立电机/舵机电源，电流裕量待确认';
  if (component === 'driver_tw_tts') return '模块供电和喇叭功放电源待确认';
  if (component === 'service_wifi') return '板载 3.3V 供电，关注峰值电流';
  return '待确认';
}

export function fallbackComponentSelection(requirement, analysis, selectionNotes, sdkSummary) {
  const { selected } = hardwareAnalysisRows(`${requirement}\n${analysis}`, sdkSummary);
  const rows = selected.map((component) => ({
    feature: component.replace(/^driver_/, '').replace(/^service_/, ''),
    candidate: componentCandidate(component),
    type: componentFunctionType(component),
    iface: componentInterface(component),
    supply: componentSupply(component),
    resource: componentResourceText(component, sdkSummary),
    api: componentPrimaryApi(component, sdkSummary),
    example: (sdkSummary.examples || []).find((example) => example.includes(component.replace(/^driver_|^service_/, '').split('_')[0])) || '先查找相近 example'
  }));

  return [
    '# 器件选型',
    '',
    '## 器件选型摘要',
    `- 选型目标：基于已接受的功能分析，为 ${rows.map((row) => row.feature).join('、') || '待确认功能'} 选择器件或模块。`,
    '- 目标板卡：优先使用当前选择或默认 ESP32-S3 板级定义。',
    '- 优先约束：当前 SDK 已支持、接口资源可用、资料清晰、适合开发板验证。',
    selectionNotes ? `- 用户调整要求：${selectionNotes}` : '- 用户调整要求：暂无。',
    '- 主要风险：电气参数、供电电流、I2C 地址/PWM/GPIO 资源和结构安装仍需人工确认。',
    '',
    '## 功能到器件映射',
    '| 功能 | 推荐器件/模块 | 类型 | 接口 | 供电/逻辑电平 | SDK 组件 | 板级资源 | 推荐等级 | 备注 |',
    '|---|---|---|---|---|---|---|---|---|',
    ...rows.map((row) => `| ${row.feature} | ${row.candidate} | ${row.type} | ${row.iface} | ${row.supply} | ${selected.find((name) => name.includes(row.feature)) || row.feature} | ${row.resource} | 推荐 | 优先复用当前 SDK 能力 |`),
    '',
    '## 候选器件比较',
    '| 功能 | 候选项 | 关键参数 | 优点 | 缺点/风险 | 资料来源 | 结论 |',
    '|---|---|---|---|---|---|---|',
    ...rows.map((row) => `| ${row.feature} | ${row.candidate} | ${row.iface}; ${row.supply} | SDK 适配成本低 | 关键电气/结构参数需查 datasheet | 当前 SDK/examples + 待补 datasheet | 推荐用于首版验证 |`),
    '',
    '## 关键参数清单',
    '| 器件/模块 | 供电 | 接口 | 地址/通道 | 性能参数 | 电流/功耗 | 尺寸/结构 | 待确认 |',
    '|---|---|---|---|---|---|---|---|',
    ...rows.map((row) => `| ${row.candidate} | ${row.supply} | ${row.iface} | ${row.resource} | 需按 datasheet 确认 | 需按 datasheet 确认 | 需按模块规格确认 | 电气、采购、安装和替代料 |`),
    '',
    '## SDK 适配和资源占用',
    '| 器件/模块 | SDK 支持状态 | 推荐组件/API | 需要资源 | 示例工程 | 适配工作量 | 风险 |',
    '|---|---|---|---|---|---|---|',
    ...rows.map((row, index) => `| ${row.candidate} | 已支持或部分支持 | ${row.api} | ${row.resource} | ${row.example} | ${index === 0 ? '低' : '低/中'} | 需确认资源冲突 |`),
    '',
    '## 不推荐或淘汰项',
    '| 器件/模块 | 淘汰原因 | 可替代方案 |',
    '|---|---|---|',
    '| 资料不完整或电气不兼容模块 | 暂无可靠参数或与 3.3V IO/供电能力冲突 | 使用上表推荐模块 |',
    '',
    '## 待确认问题',
    '- 是否指定具体品牌、封装、尺寸、采购渠道或成本上限？',
    '- 模块供电、电流峰值、逻辑电平和连接器形式是否满足目标板卡？',
    '- 是否需要量产替代料、认证、寿命周期和 BOM 风险评估？',
    '',
    '## 下一步建议',
    '- 如果选型符合预期，请接受器件选型并进入硬件资源规划。',
    '- 如果希望换型号、降成本、提高精度或改变接口，请补充调整要求后再次检索。'
  ].join('\n');
}

function componentResourceMacros(component) {
  const macros = {
    driver_led: 'BOARD_LED_GPIO, BOARD_LED_PWM_CHANNEL, BOARD_LED_PWM_FREQUENCY_HZ',
    driver_button: 'BOARD_BUTTON_GPIO, BOARD_BUTTON_ACTIVE_LEVEL',
    driver_servo: 'BOARD_SERVO_GPIO_0/1, BOARD_SERVO_PWM_CHANNEL_0/1, BOARD_SERVO_PWM_FREQUENCY_HZ',
    driver_oled: 'BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO, BOARD_OLED_I2C_ADDRESS',
    driver_mpu6050: 'BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO, MPU6050 I2C 地址待确认',
    driver_tof2000c_vl53l0x: 'BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO, DRIVER_TOF2000C_VL53L0X_I2C_ADDR_DEFAULT',
    driver_tw_tts: 'BOARD_UART_TX_GPIO, BOARD_UART_RX_GPIO, BOARD_UART_BAUD_RATE',
    service_wifi: 'ESP32 内置 WiFi，无外部 GPIO',
    driver_motor: 'BOARD_MOTOR_* GPIO/PWM 宏',
    driver_encoder: 'BOARD_ENCODER_* GPIO 宏',
    driver_tb6612: 'BOARD_MOTOR_* GPIO/PWM 宏'
  };
  return macros[component] || '待确认';
}

function componentResourcePins(component, sdkSummary) {
  const resource = componentResourceText(component, sdkSummary);
  if (component === 'driver_oled') return `${resource}; I2C address 0x3C`;
  if (component === 'driver_tof2000c_vl53l0x') return `${resource}; I2C address 0x29`;
  if (component === 'driver_mpu6050') return `${resource}; I2C address 0x68/0x69 待确认`;
  return resource;
}

function compactConnectionPins(text) {
  return String(text || '')
    .replaceAll('BOARD_I2C_SDA_GPIO', 'SDA')
    .replaceAll('BOARD_I2C_SCL_GPIO', 'SCL')
    .replaceAll('BOARD_I2C_FREQUENCY_HZ', 'FREQ')
    .replaceAll('BOARD_OLED_I2C_ADDRESS', 'A')
    .replaceAll('BOARD_UART_TX_GPIO', 'TX')
    .replaceAll('BOARD_UART_RX_GPIO', 'RX')
    .replaceAll('BOARD_UART_BAUD_RATE', 'BAUD')
    .replaceAll('BOARD_BUTTON_GPIO', 'BTN')
    .replaceAll('BOARD_BUTTON_ACTIVE_LEVEL', 'LEVEL')
    .replaceAll('BOARD_LED_GPIO', 'GPIO')
    .replaceAll('BOARD_LED_PWM_CHANNEL', 'CH')
    .replaceAll('BOARD_LED_PWM_FREQUENCY_HZ', 'FREQ')
    .replaceAll('BOARD_SERVO_GPIO_0', 'S0')
    .replaceAll('BOARD_SERVO_GPIO_1', 'S1')
    .replaceAll('BOARD_SERVO_PWM_CHANNEL_0', 'CH0')
    .replaceAll('BOARD_SERVO_PWM_CHANNEL_1', 'CH1')
    .replaceAll('BOARD_SERVO_PWM_FREQUENCY_HZ', 'FREQ')
    .replaceAll('DRIVER_TOF2000C_VL53L0X_I2C_ADDR_DEFAULT', 'A')
    .replace(/\bI2C address\s+/g, 'A=')
    .replace(/\bBOARD_MOTOR_/g, 'M_')
    .replace(/\bBOARD_ENCODER_/g, 'ENC_')
    .replace(/\bI2C:\s*/g, '')
    .replace(/\bUART:\s*/g, '')
    .replace(/\bLED:\s*/g, '')
    .replace(/\bServo:\s*/g, '')
    .replace(/,\s*FREQ=[^;"]+/g, '')
    .replace(/,\s*BAUD=([^;"]+)/g, ' @ $1');
}

export function fallbackHardwareResourcePlanning(requirement, analysis, componentSelection, resourceNotes, sdkSummary) {
  const { selected } = hardwareAnalysisRows(`${requirement}\n${analysis}\n${componentSelection}`, sdkSummary);
  const rows = selected.map((component) => ({
    component,
    feature: component.replace(/^driver_/, '').replace(/^service_/, ''),
    candidate: componentCandidate(component),
    iface: componentInterface(component),
    macros: componentResourceMacros(component),
    pins: componentResourcePins(component, sdkSummary),
    sharing: ['driver_oled', 'driver_mpu6050', 'driver_tof2000c_vl53l0x'].includes(component) ? '共享' : '独占',
    status: ['driver_oled', 'driver_mpu6050', 'driver_tof2000c_vl53l0x'].includes(component) ? '共享可用' : '已分配'
  }));
  const i2cRows = rows.filter((row) => row.iface === 'I2C');
  const pwmRows = rows.filter((row) => /PWM/.test(row.iface));
  const uartRows = rows.filter((row) => row.iface === 'UART');
  const gpioRows = rows.filter((row) => /GPIO/.test(row.iface) && !/PWM/.test(row.iface));

  return [
    '# 硬件资源规划',
    '',
    '## 硬件资源规划摘要',
    '- 目标板卡：优先使用当前选择或默认 ESP32-S3 板级定义。',
    `- 规划结论：为 ${rows.map((row) => row.candidate).join('、') || '待确认模块'} 生成接口和引脚分配。`,
    resourceNotes ? `- 用户调整要求：${resourceNotes}` : '- 用户调整要求：暂无。',
    '- 主要风险：共享 I2C 地址、PWM/LEDC 通道、UART 端口、供电电流和敏感 GPIO 仍需实物确认。',
    '',
    '## 接口和引脚分配',
    '| 功能/器件 | SDK 组件 | 接口 | 板级资源/宏 | GPIO/通道/地址 | 共享/独占 | 状态 | 备注 |',
    '|---|---|---|---|---|---|---|---|',
    ...rows.map((row) => `| ${row.candidate} | ${row.component} | ${row.iface} | ${row.macros} | ${row.pins} | ${row.sharing} | ${row.status} | 优先复用 boards/ 中已定义资源 |`),
    '',
    '## 共享总线规划',
    '| 总线 | 引脚/端口 | 挂载器件 | 地址/参数 | 冲突检查 | 备注 |',
    '|---|---|---|---|---|---|',
    i2cRows.length
      ? `| I2C | ${componentResourceText('driver_oled', sdkSummary)} | ${i2cRows.map((row) => row.candidate).join('、')} | ${i2cRows.map((row) => row.pins.split(';').slice(-1)[0].trim()).join('；')} | 待确认地址是否重复 | I2C 可共享 SDA/SCL，但地址必须唯一 |`
      : '| I2C | 未使用 | 无 | 无 | 通过 | 当前选型未命中 I2C 器件 |',
    uartRows.length
      ? `| UART | ${componentResourceText('driver_tw_tts', sdkSummary)} | ${uartRows.map((row) => row.candidate).join('、')} | BOARD_UART_BAUD_RATE | 待确认端口独占 | 避免占用下载/日志串口 |`
      : '| UART | 未使用 | 无 | 无 | 通过 | 当前选型未命中 UART 器件 |',
    '',
    '## 独占资源规划',
    '| 资源类型 | 资源 | 使用者 | 参数 | 状态 | 备注 |',
    '|---|---|---|---|---|---|',
    ...pwmRows.map((row) => `| PWM/LEDC | ${row.macros} | ${row.candidate} | ${row.pins} | 已分配 | 检查 channel/timer 不与其他 PWM 输出冲突 |`),
    ...gpioRows.map((row) => `| GPIO | ${row.macros} | ${row.candidate} | ${row.pins} | 已分配 | 检查输入/输出方向和上下拉 |`),
    pwmRows.length === 0 && gpioRows.length === 0 ? '| GPIO/PWM | 无 | 无 | 无 | 待确认 | 当前选型未命中独占 GPIO/PWM 资源 |' : '',
    '',
    '## 冲突检查',
    '| 检查项 | 结果 | 说明 | 处理建议 |',
    '|---|---|---|---|',
    '| GPIO 独占冲突 | 待确认 | 已按 board 宏分配，仍需检查是否与下载、USB、Flash/PSRAM 或保留引脚冲突 | 实物接线前复核 board.h 和芯片 datasheet |',
    '| I2C 地址冲突 | 待确认 | I2C 总线可共享，但 OLED/ToF/IMU 地址必须唯一 | 上电后先运行 probe/demo 检查地址 |',
    '| PWM/LEDC 通道冲突 | 待确认 | 舵机、电机、LED 可能使用不同 channel/timer | 以 boards/ 宏为准，避免手写新 channel |',
    '| UART 端口冲突 | 待确认 | TTS/外设 UART 不应占用下载和日志串口 | 确认 BOARD_UART_PORT、TX、RX 和波特率 |',
    '| 供电电流 | 待确认 | 舵机、电机、显示和语音模块峰值电流可能超过板载 3.3V | 执行器优先独立供电，共地连接 |',
    '',
    '## 接线和初始化建议',
    '| 顺序 | 模块 | 接线/初始化动作 | 依赖 | 验证方式 |',
    '|---|---|---|---|---|',
    ...rows.map((row, index) => `| ${index + 1} | ${row.candidate} | 按 ${row.macros} 接线并调用 ${row.component} 初始化 | ${row.iface} 资源可用 | 运行对应 example 或 probe/read/set 测试 |`),
    '',
    '## 待确认问题',
    '- 目标板卡是否就是当前选择的 board，是否存在外接扩展板或转接线？',
    '- I2C 器件地址是否可修改，是否与 OLED/ToF/IMU 默认地址冲突？',
    '- 舵机、电机、TTS/功放等模块是否有独立供电和共地方案？',
    '- 是否需要避开启动绑带、USB、Flash/PSRAM 或输入仅用 GPIO？',
    '',
    '## 下一步建议',
    '- 如果资源分配符合预期，请接受硬件资源规划并进入硬件搭建。',
    '- 如果需要换接口、换引脚、增加模块或规避冲突，请补充调整要求后重新规划。'
  ].join('\n');
}

function hardwareAnalysisRows(requirement, sdkSummary) {
  const capabilities = detectCapabilities(requirement, sdkSummary);
  const selected = capabilities.components.length > 0 ? capabilities.components : ['service_device'];
  const knownComponents = componentNames(sdkSummary);
  return {
    capabilities,
    selected,
    hardwareRows: selected.map((component) => [
      component.replace(/^driver_/, '').replace(/^service_/, ''),
      componentFunctionType(component),
      capabilities.notes.find((note) => note.toLowerCase().includes(component.replace(/^driver_|^service_/, '').split('_')[0])) || '根据需求实现对应硬件能力',
      component,
      componentResourceText(component, sdkSummary),
      knownComponents.includes(component) ? '已支持' : '缺口',
      component === 'service_device' ? '基础设备初始化能力' : '需结合需求细化状态机和验收方式'
    ]),
    mappingRows: selected.map((component) => [
      component,
      componentPrimaryApi(component, sdkSummary),
      (sdkSummary.examples || []).find((example) => example.includes(component.replace(/^driver_|^service_/, '').split('_')[0])) || '先查找相近 example',
      knownComponents.includes(component) ? '已支持' : '缺口',
      component === 'service_device' ? '需补充具体硬件需求' : '需确认板级资源和组合冲突'
    ])
  };
}

function needsStateMachine(requirement) {
  return /状态|模式|切换|按键|长按|短按|自动|循环|持续|闭环|控制|避障|巡航|跟随|上位机|执行|任务|停止|急停|故障|错误|恢复|timeout|超时|event|command/i.test(String(requirement || ''));
}

function fallbackStateMachineSections(requirement) {
  if (!needsStateMachine(requirement)) {
    return [
      '## 状态机需求判断',
      '- 判断：否',
      '- 原因：当前需求未明确包含多模式切换、持续控制、事件响应或异常恢复；可先按单次功能验证处理。',
      '',
      '## 状态机设计',
      '| 状态 | 职责 | 进入条件 | 退出条件 | 硬件动作 |',
      '|---|---|---|---|---|',
      '| 暂无 | 暂无状态机需求 | - | - | - |',
      '',
      '## 状态转移条件',
      '| From | To | 条件/事件 | 动作 |',
      '|---|---|---|---|',
      '| 暂无 | 暂无 | - | - |',
      '',
      '## 状态机状态转移图',
      '暂无状态机图。'
    ];
  }

  return [
    '## 状态机需求判断',
    '- 判断：是',
    '- 原因：需求包含事件响应、执行器控制、传感器反馈或运行/停止过程，建议先定义应用状态机再进入固件实现。',
    '',
    '## 状态机设计',
    '| 状态 | 职责 | 进入条件 | 退出条件 | 硬件动作 |',
    '|---|---|---|---|---|',
    '| Init | 初始化 SDK、BSP、传感器和执行器 | 上电启动 | 初始化成功或失败 | 初始化外设，输出启动日志 |',
    '| Idle | 等待用户输入、传感器事件或上位机命令 | 初始化成功、动作完成、复位 | 收到有效事件或命令 | 保持安全输出，周期观测输入 |',
    '| Active | 执行主要功能或控制动作 | 用户输入、传感器触发或上位机命令 | 动作完成、停止、异常 | 驱动相关执行器并采集反馈 |',
    '| Error | 记录故障并进入安全态 | 初始化失败、通信失败、传感器异常 | 用户复位或错误解除 | 停止危险输出，保留错误信息 |',
    '',
    '## 状态转移条件',
    '| From | To | 条件/事件 | 动作 |',
    '|---|---|---|---|',
    '| Init | Idle | 初始化成功 | 启动周期任务 |',
    '| Init | Error | 初始化失败 | 记录错误并关闭执行器 |',
    '| Idle | Active | 输入事件、传感器触发或上位机命令 | 设置目标动作 |',
    '| Active | Idle | 动作完成或条件解除 | 清除 busy 状态 |',
    '| Active | Error | 超时、通信异常或硬件错误 | 停止执行器 |',
    '| Error | Idle | 用户复位或错误解除 | 重新进入等待状态 |',
    '',
    '## 状态机状态转移图',
    '```mermaid',
    'stateDiagram-v2',
    '  [*] --> Init',
    '  Init --> Idle: init ok',
    '  Init --> Error: init failed',
    '  Idle --> Active: event/cmd',
    '  Active --> Idle: done',
    '  Active --> Error: fault/timeout',
    '  Error --> Idle: reset',
    '```'
  ];
}

export function fallbackRequirementAnalysis(requirement, sdkSummary) {
  const { selected, hardwareRows, mappingRows } = hardwareAnalysisRows(requirement, sdkSummary);
  return [
    '# 功能分析',
    '',
    '## 需求摘要',
    requirement ? `- 产品目标：${requirement.split(/\r?\n/).find((line) => line.trim()) || requirement}` : '- 产品目标：尚未填写具体产品需求。',
    '- 产品形态：待确认，当前按开发板/SDK 应用验证处理。',
    `- 初步命中的 SDK 能力：${selected.join(', ')}`,
    '',
    '## 硬件功能清单',
    '| 功能 | 类型 | 用户行为/系统行为 | SDK 组件 | 板级资源 | 状态 | 备注 |',
    '|---|---|---|---|---|---|---|',
    ...hardwareRows.map((row) => `| ${row.join(' | ')} |`),
    '',
    '## SDK 能力映射',
    '| 需求项 | 推荐组件/API | 示例或参考 | 支持状态 | 风险 |',
    '|---|---|---|---|---|',
    ...mappingRows.map((row) => `| ${row.join(' | ')} |`),
    '',
    ...fallbackStateMachineSections(requirement),
    '',
    '## 待确认问题',
    '- 产品形态、供电方式、目标板卡和是否需要量产设计。',
    '- 每个传感器/执行器的具体型号、接口、电压和安装方式。',
    '- 是否需要上位机、网络协议、自动化测试或 physical agent 闭环。',
    '',
    '## 缺口和风险',
    '- 未命中的硬件模块需要补充 SDK driver/service 或调整需求。',
    '- 板级 GPIO、I2C、UART、PWM 等资源需要在资源规划阶段进一步检查冲突。',
    '',
    '## 建议的下一步',
    '- 如果以上功能拆解符合预期，请接受分析并进入器件选型。',
    '- 如果有遗漏或理解不准确，请完善需求后重新进行功能分析。'
  ].join('\n');
}

export function fallbackHardwareBuild(requirement, analysis, componentSelection, resourcePlan, sdkSummary) {
  const { selected } = hardwareAnalysisRows(`${requirement}\n${analysis}\n${componentSelection}\n${resourcePlan}`, sdkSummary);
  const rows = selected.map((component) => ({
    component,
    candidate: componentCandidate(component),
    iface: componentInterface(component),
    macros: componentResourceMacros(component),
    pins: componentResourcePins(component, sdkSummary),
    power: component === 'service_wifi' ? '目标板内置' : '按模块规格，优先 3.3V 逻辑',
    sharing: ['driver_oled', 'driver_mpu6050', 'driver_tof2000c_vl53l0x'].includes(component) ? '共享可用' : '可接线'
  }));
  const edgeRows = rows
    .filter((row) => row.component !== 'service_wifi')
    .map((row, index) => `  MCU -- "${row.iface} / ${compactConnectionPins(row.pins).replaceAll('"', "'")}" --> M${index}[${row.candidate}]`);
  const diagram = [
    'flowchart LR',
    '  MCU[ESP32 / 目标板]',
    ...edgeRows,
    rows.some((row) => row.component !== 'service_wifi') ? '  POWER["供电 / 共地：统一连接，详见接线清单"]' : ''
  ].filter(Boolean).join('\n');
  const i2cRows = rows.filter((row) => row.iface === 'I2C');
  const uartRows = rows.filter((row) => row.iface === 'UART');

  return [
    '# 硬件搭建',
    '',
    '## 硬件搭建摘要',
    `- 根据已接受的硬件资源规划，为 ${rows.map((row) => row.candidate).join('、') || selected.join('、')} 生成模块连接框图。`,
    '- 接线必须优先采用资源规划中的接口、GPIO、通道和地址；下表只做搭建表达，不重新分配资源。',
    '',
    '## 模块连接框图',
    '```mermaid',
    diagram,
    '```',
    '',
    '## 模块接线清单',
    '| 模块 | 模块接口/管脚 | 目标板接口/宏 | GPIO/通道/地址 | 供电 | 共地 | 状态 | 备注 |',
    '|---|---|---|---|---|---|---|---|',
    ...rows.map((row) => `| ${row.candidate} | ${row.iface} | ${row.macros} | ${row.pins} | ${row.power} | 必须共地 | ${row.sharing} | 按资源规划接线，模块丝印需实物复核 |`),
    '',
    '## 接口分组',
    '| 接口/总线 | 连接模块 | 引脚/端口 | 地址/参数 | 注意事项 |',
    '|---|---|---|---|---|',
    i2cRows.length ? `| I2C | ${i2cRows.map((row) => row.candidate).join('、')} | ${componentResourceText('driver_oled', sdkSummary)} | ${i2cRows.map((row) => row.pins.split(';').slice(-1)[0].trim()).join('；')} | 地址必须唯一，SDA/SCL 共享 |` : '| I2C | 无 | 无 | 无 | 当前搭建未使用 I2C |',
    uartRows.length ? `| UART | ${uartRows.map((row) => row.candidate).join('、')} | ${componentResourceText('driver_tw_tts', sdkSummary)} | BOARD_UART_BAUD_RATE | 避免占用下载/日志串口 |` : '| UART | 无 | 无 | 无 | 当前搭建未使用 UART |',
    `| GPIO/PWM | ${rows.filter((row) => /GPIO|PWM/.test(row.iface)).map((row) => row.candidate).join('、') || '无'} | 按资源规划宏 | 按资源规划 | 启动绑带和保留引脚需复核 |`,
    '',
    '## 电源和共地',
    '| 模块 | 供电电压 | 峰值电流/功耗 | 供电来源 | 共地要求 | 风险 |',
    '|---|---|---|---|---|---|',
    ...rows.map((row) => `| ${row.candidate} | 待按模块确认 | 待确认 | ${row.power} | 与目标板 GND 共地 | 执行器/语音/电机类模块不建议直接吃板载 3.3V 大电流 |`),
    '',
    '## 上电前检查',
    '- 万用表确认 VCC/GND 未短路，模块供电电压与逻辑电平匹配。',
    '- 复核 SDA/SCL、TX/RX、PWM/GPIO 方向，尤其 UART 需要交叉连接 TX/RX。',
    '- 执行器、电机、舵机和功放模块如需独立供电，先共地再接信号线。',
    '',
    '## 单模块验证顺序',
    ...rows.map((row, index) => `- ${index + 1}. ${row.candidate}：按 ${row.iface} 接线后，先运行对应 demo 或最小读写动作。`),
    '',
    '## 待确认问题',
    '- 模块实物丝印、供电电压、峰值电流和接口方向。',
    '- I2C 地址、UART 波特率、PWM 频率/占空比范围是否与资源规划一致。',
    '- 外设安装位置、线长、连接器规格和抗干扰需求。'
  ].filter(Boolean).join('\n');
}

export function fallbackChat(question, sdkSummary) {
  const q = String(question || '');
  if (/组件|api|接口|driver|service/i.test(q)) {
    return [
      '当前 SDK 已扫描到这些组件：',
      componentNames(sdkSummary).map((name) => `- ${name}`).join('\n'),
      '',
      '建议优先从 examples/ 中找相近 demo，再在 project/ 下创建应用工程。'
    ].join('\n');
  }

  if (/引脚|gpio|资源|i2c|uart|pwm/i.test(q)) {
    return boardResourceLines(sdkSummary).map((line) => `- ${line}`).join('\n');
  }

  return [
    '我会按当前 SDK 的边界回答：只基于已有 BSP、driver_*、service_* 能力规划，不建议直接改 SDK 内部。',
    '如果问题涉及具体产品，请补充产品形态、输入输出模块、供电方式、是否联网、是否量产。'
  ].join('\n');
}

function makeCmakeRoot(projectName) {
  return `cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    \${CMAKE_CURRENT_LIST_DIR}/../../bsp
    \${CMAKE_CURRENT_LIST_DIR}/../../components
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(${projectName})
`;
}

function makeMainCmake(requires) {
  return `idf_component_register(
    SRCS "main.c"
    REQUIRES ${requires.join(' ')}
)
`;
}

function makeMainC(requirement, capabilities) {
  const includes = [
    '#include <stdio.h>',
    '#include "freertos/FreeRTOS.h"',
    '#include "freertos/task.h"',
    '#include "esp_log.h"',
    '#include "service_device.h"'
  ];
  if (capabilities.components.includes('driver_led')) includes.push('#include "driver_led.h"');
  if (capabilities.components.includes('driver_button')) includes.push('#include "driver_button.h"');
  if (capabilities.components.includes('driver_oled')) includes.push('#include "driver_oled.h"');
  if (capabilities.components.includes('driver_servo')) includes.push('#include "driver_servo.h"');
  if (capabilities.components.includes('driver_mpu6050')) includes.push('#include "driver_mpu6050.h"');
  if (capabilities.components.includes('driver_tof2000c_vl53l0x')) includes.push('#include "driver_tof2000c_vl53l0x.h"');

  const setup = ['    service_device_init();'];
  if (capabilities.components.includes('driver_led')) setup.push('    driver_led_set_brightness(50);', '    driver_led_set(1);');
  if (capabilities.components.includes('driver_oled')) setup.push('    driver_oled_init_profile(DRIVER_OLED_PROFILE_096_SSD1306);', '    driver_oled_clear_screen();');
  if (capabilities.components.includes('driver_servo')) setup.push('    driver_servo_init();', '    driver_servo_set_angle(DRIVER_SERVO_0, 90);');
  if (capabilities.components.includes('driver_mpu6050')) setup.push('    driver_mpu6050_init_default();');
  if (capabilities.components.includes('driver_tof2000c_vl53l0x')) setup.push('    driver_tof2000c_vl53l0x_init_default();');

  const loop = [];
  if (capabilities.components.includes('driver_servo')) {
    loop.push(
      '        static uint16_t servo_angle = 60;',
      '        static int servo_step = 20;',
      '        driver_servo_set_angle(DRIVER_SERVO_0, servo_angle);',
      '        servo_angle = (uint16_t)((int)servo_angle + servo_step);',
      '        if (servo_angle >= 120 || servo_angle <= 60) {',
      '            servo_step = -servo_step;',
      '        }'
    );
  }
  if (capabilities.components.includes('driver_button')) {
    loop.push(
      '        driver_button_process();',
      '        driver_button_event_t event = driver_button_get_event();',
      '        if (event == DRIVER_BUTTON_EVENT_SHORT_PRESS) {',
      '            ESP_LOGI(TAG, "button short press");',
      capabilities.components.includes('driver_led') ? '            driver_led_toggle();' : '            /* TODO: handle short press */',
      '        }'
    );
  }
  if (capabilities.components.includes('driver_mpu6050')) {
    loop.push(
      '        driver_mpu6050_data_t imu;',
      '        if (driver_mpu6050_read(&imu) == ESP_OK) {',
      '            ESP_LOGI(TAG, "imu ax=%.2f ay=%.2f az=%.2f", imu.accel_x_g, imu.accel_y_g, imu.accel_z_g);',
      '        }'
    );
  }
  if (capabilities.components.includes('driver_tof2000c_vl53l0x')) {
    loop.push(...[
      '        driver_tof2000c_vl53l0x_result_t range;',
      '        if (driver_tof2000c_vl53l0x_read_single(&range) == ESP_OK) {',
      '            ESP_LOGI(TAG, "distance=%u mm", range.distance_mm);',
      capabilities.components.includes('driver_oled') ? '            char line[32];' : '',
      capabilities.components.includes('driver_oled') ? '            snprintf(line, sizeof(line), "Dist: %u mm", range.distance_mm);' : '',
      capabilities.components.includes('driver_oled') ? '            driver_oled_clear();' : '',
      capabilities.components.includes('driver_oled') ? '            driver_oled_draw_string(0, 0, "Moce Agent");' : '',
      capabilities.components.includes('driver_oled') ? '            driver_oled_draw_string(0, 16, line);' : '',
      capabilities.components.includes('driver_oled') ? '            driver_oled_flush();' : '',
      '        }'
    ].filter(Boolean));
  }
  if (loop.length === 0) {
    loop.push('        ESP_LOGI(TAG, "agent scaffold heartbeat");');
  }
  loop.push('        vTaskDelay(pdMS_TO_TICKS(500));');

  return `${includes.join('\n')}

static const char *TAG = "moce_agent_app";

void app_main(void)
{
    ESP_LOGI(TAG, "Moce agent generated scaffold start");
    ESP_LOGI(TAG, "requirement: ${String(requirement || 'not specified').replaceAll('\\', '\\\\').replaceAll('"', '\\"')}");

${setup.join('\n')}

    while (1) {
${loop.join('\n')}
    }
}
`;
}

function sdkHasCh32Board(sdkSummary) {
  return (sdkSummary?.boards || []).some((board) => isCh32Board(board.name));
}

function makeCh32Cmake() {
  return `add_executable(\${APP}.elf
    main.c
)

target_link_libraries(\${APP}.elf PRIVATE
    -Wl,--whole-archive
    board_\${BOARD}
    -Wl,--no-whole-archive
    user_api
)

target_link_options(\${APP}.elf PRIVATE
    \${COMMON_LINK_FLAGS}
)

add_custom_command(TARGET \${APP}.elf POST_BUILD
    COMMAND \${CMAKE_SIZE} \${APP}.elf
    COMMAND \${CMAKE_OBJCOPY} -O ihex \${APP}.elf \${APP}.hex
    COMMAND \${CMAKE_OBJCOPY} -O binary \${APP}.elf \${APP}.bin
)
`;
}

function makeCh32Main(requirement) {
  const safeRequirement = String(requirement || 'agent generated CH32 app')
    .replaceAll('\\', '\\\\')
    .replaceAll('"', '\\"')
    .slice(0, 120);
  return `#include "board_pins.h"
#include "mcu_port_time.h"
#include "vibe_api.h"
#include "vibe_runtime.h"

#define APP_PERIOD_MS 1000U

static void app_task(void)
{
    vibe_led_toggle();
    vibe_println("MOCE CH32 agent heartbeat");
}

void app_setup(void)
{
    vibe_serial_begin(115200U);
    mcu_port_delay_ms(300U);
    vibe_println("MOCE SDK CH32 generated app");
    vibe_println("requirement: ${safeRequirement}");
    (void)vibe_task_every_ms(APP_PERIOD_MS, app_task);
}
`;
}

export function fallbackCodegen(requirement, projectName, sdkSummary) {
  const name = slugifyProjectName(projectName || (sdkHasCh32Board(sdkSummary) ? 'oled_096_readout' : 'robot_agent_app'));
  if (sdkHasCh32Board(sdkSummary)) {
    return {
      projectName: name,
      files: [
        {
          path: `project/${name}/CMakeLists.txt`,
          content: makeCh32Cmake()
        },
        {
          path: `project/${name}/main.c`,
          content: makeCh32Main(requirement)
        }
      ]
    };
  }

  const capabilities = detectCapabilities(requirement, sdkSummary);
  const requires = new Set(['service_device']);
  for (const component of capabilities.components) {
    requires.add(component);
  }

  return {
    projectName: name,
    files: [
      {
        path: `project/${name}/CMakeLists.txt`,
        content: makeCmakeRoot(name)
      },
      {
        path: `project/${name}/main/CMakeLists.txt`,
        content: makeMainCmake([...requires])
      },
      {
        path: `project/${name}/main/main.c`,
        content: makeMainC(requirement, capabilities)
      }
    ]
  };
}

export function fallbackDebugFix(projectFiles) {
  const files = (projectFiles || []).map((file) => ({ ...file }));
  const main = files.find((file) => /\/main\/main\.c$/.test(file.path));
  const mainCmake = files.find((file) => /\/main\/CMakeLists\.txt$/.test(file.path));
  let changed = false;

  if (main?.content.includes('#include "bsp_board.h"')) {
    main.content = main.content.replace(/\s*#include\s+"bsp_board\.h"\s*/g, '\n');
    changed = true;
  }

  if (main && /\bservice_device_init\s*\(/.test(main.content) && !/#include\s+"service_device\.h"/.test(main.content)) {
    const includeMatch = [...main.content.matchAll(/^#include\s+"[^"]+"\s*$/gm)].pop();
    if (includeMatch) {
      const insertAt = includeMatch.index + includeMatch[0].length;
      main.content = `${main.content.slice(0, insertAt)}\n#include "service_device.h"${main.content.slice(insertAt)}`;
    } else {
      main.content = `#include "service_device.h"\n${main.content}`;
    }
    changed = true;
  }

  if (mainCmake && main && /\bservice_device_init\s*\(/.test(main.content) && !/\bservice_device\b/.test(mainCmake.content)) {
    mainCmake.content = mainCmake.content.replace(/REQUIRES\s+([^\n)]+)/, (match, deps) => {
      const parts = deps.trim().split(/\s+/);
      return `REQUIRES service_device ${parts.join(' ')}`;
    });
    changed = true;
  }

  return changed ? files : [];
}

function cmakeRequires(content) {
  const match = String(content || '').match(/idf_component_register\s*\(([\s\S]*?)\)/);
  if (!match) return [];
  const body = match[1].replace(/#[^\n]*/g, ' ');
  const requiresMatch = body.match(/\bREQUIRES\b([\s\S]*?)(?:\bPRIV_REQUIRES\b|\bSRCS\b|\bINCLUDE_DIRS\b|\bPRIV_INCLUDE_DIRS\b|$)/);
  if (!requiresMatch) return [];
  return requiresMatch[1]
    .split(/\s+/)
    .map((item) => item.trim())
    .filter(Boolean)
    .filter((item) => !/^["')]+$/.test(item));
}

function charArraySizes(content) {
  const sizes = new Map();
  const text = String(content || '');
  for (const match of text.matchAll(/\bchar\s+(\w+)\s*\[\s*(\d+)\s*\]/g)) {
    sizes.set(match[1], Number(match[2]));
  }
  for (const match of text.matchAll(/\bchar\s+(\w+)\s*\[\s*(\d+)\s*\]\s*;/g)) {
    sizes.set(match[1], Number(match[2]));
  }
  return sizes;
}

function unboundedStringFormat(format) {
  return /(^|[^%])%s/.test(String(format || ''));
}

function snprintfTruncationRisks(content) {
  const text = String(content || '');
  const sizes = charArraySizes(text);
  const risks = [];
  const callPattern = /snprintf\s*\(\s*(\w+)\s*,\s*sizeof\s*\(\s*\1\s*\)\s*,\s*"((?:\\.|[^"\\])*)"\s*,([\s\S]*?)\)\s*;/g;

  for (const match of text.matchAll(callPattern)) {
    const [, dest, format, args] = match;
    const destSize = sizes.get(dest);
    if (!destSize || !unboundedStringFormat(format)) {
      continue;
    }

    for (const [name, sourceSize] of sizes.entries()) {
      const sourceRefPattern = new RegExp(`(?:\\b${name}\\b|[\\w)\\]]+(?:\\.|->)${name}\\b)`);
      if (sourceSize >= destSize && sourceRefPattern.test(args)) {
        risks.push({ dest, source: name, destSize, sourceSize });
      }
    }
  }

  return risks;
}

function validateCh32GeneratedFiles(files) {
  const errors = [];
  const warnings = [];
  const normalizedFiles = files || [];
  const main = normalizedFiles.find((file) => /^project\/[^/]+\/main\.c$/.test(String(file.path || '').replaceAll('\\', '/')));
  const cmake = normalizedFiles.find((file) => /^project\/[^/]+\/CMakeLists\.txt$/.test(String(file.path || '').replaceAll('\\', '/')));
  const allText = normalizedFiles.map((file) => String(file.content || '')).join('\n');

  if (!main) {
    errors.push('missing project/<app>/main.c for CH32 app');
  }
  if (!cmake) {
    errors.push('missing project/<app>/CMakeLists.txt for CH32 app');
  }
  if (normalizedFiles.some((file) => /\/main\/CMakeLists\.txt$/.test(String(file.path || '').replaceAll('\\', '/')))) {
    errors.push('CH32 app must not use project/<app>/main/CMakeLists.txt');
  }
  if (cmake && !/add_executable\s*\(\s*\$\{APP\}\.elf/.test(cmake.content || '')) {
    errors.push('CH32 CMakeLists.txt must add_executable(${APP}.elf main.c)');
  }
  if (cmake && /\bcmake_minimum_required\s*\(/.test(cmake.content || '')) {
    errors.push('CH32 project/<app>/CMakeLists.txt must not call cmake_minimum_required(); the SDK root CMakeLists.txt already owns the project');
  }
  if (cmake && /\bproject\s*\(/.test(cmake.content || '')) {
    errors.push('CH32 project/<app>/CMakeLists.txt must not call project(); it is added by the SDK root CMakeLists.txt');
  }
  if (cmake && /\bset\s*\(\s*(APP|BOARD)\b/.test(cmake.content || '')) {
    errors.push('CH32 project/<app>/CMakeLists.txt must not set APP or BOARD; scripts/build.py passes them from the SDK root');
  }
  if (cmake && /\binclude\s*\(/.test(cmake.content || '')) {
    errors.push('CH32 project/<app>/CMakeLists.txt must not include helper cmake files; use the same local CMake shape as examples/oled_096_readout');
  }
  if (cmake && !/board_\$\{BOARD\}/.test(cmake.content || '')) {
    errors.push('CH32 CMakeLists.txt must link board_${BOARD}');
  }
  if (cmake && !/\buser_api\b/.test(cmake.content || '')) {
    errors.push('CH32 CMakeLists.txt must link user_api');
  }
  if (main && !/\bvoid\s+app_setup\s*\(\s*void\s*\)/.test(main.content || '')) {
    errors.push('CH32 main.c must define void app_setup(void)');
  }
  if (/\bapp_main\s*\(|idf_component_register|ESP_LOG[IWED]?\s*\(|#include\s+"?esp_|#include\s+"?freertos\//.test(allText)) {
    errors.push('CH32 generated files must not use ESP-IDF app_main, idf_component_register, ESP_LOG, esp_* headers, or FreeRTOS headers');
  }
  if (main && /BOARD_/.test(main.content || '') && !/#include\s+"board_pins\.h"/.test(main.content || '')) {
    warnings.push('main.c uses BOARD_* macros; include "board_pins.h" so CH32 board pin definitions are visible');
  }

  for (const risk of snprintfTruncationRisks(main?.content || '')) {
    warnings.push(`snprintf may truncate unbounded string into ${risk.dest}; use %.Ns precision or a larger buffer`);
  }

  return { ok: errors.length === 0, errors, warnings };
}

export function validateGeneratedFiles(files, sdkSummary = null, boardName = '') {
  if (isCh32Board(boardName) || sdkHasCh32Board(sdkSummary)) {
    return validateCh32GeneratedFiles(files);
  }

  const errors = [];
  const warnings = [];
  const main = (files || []).find((file) => /\/main\/main\.c$/.test(file.path));
  const rootCmake = (files || []).find((file) => /\/CMakeLists\.txt$/.test(file.path) && !/\/main\/CMakeLists\.txt$/.test(file.path));
  const mainCmake = (files || []).find((file) => /\/main\/CMakeLists\.txt$/.test(file.path));
  const content = main?.content || '';
  const generatedHeaderNames = new Set((files || [])
    .filter((file) => /\.h$/.test(file.path || ''))
    .flatMap((file) => {
      const normalized = String(file.path || '').replaceAll('\\', '/');
      return [normalized.split('/').pop(), normalized.replace(/^project\/[^/]+\/main\//, '')].filter(Boolean);
    }));

  if (!main) {
    errors.push('missing project/<name>/main/main.c');
  }

  if (!rootCmake || !/EXTRA_COMPONENT_DIRS/.test(rootCmake.content || '') || !/project\.cmake/.test(rootCmake.content || '')) {
    errors.push('root CMakeLists.txt must define EXTRA_COMPONENT_DIRS and include ESP-IDF project.cmake');
  }

  if (rootCmake && !/cmake_minimum_required\s*\(/.test(rootCmake.content || '')) {
    errors.push('root CMakeLists.txt must start with cmake_minimum_required(VERSION 3.16) or a compatible ESP-IDF CMake version');
  }

  if (rootCmake && !/\bproject\s*\(/.test(rootCmake.content || '')) {
    errors.push('root CMakeLists.txt must call project(<name>) after including ESP-IDF project.cmake');
  }

  if (/idf_component_register\s*\(|register_component\s*\(|\bREQUIRES\b|\bSRCS\b/.test(rootCmake?.content || '')) {
    errors.push('root CMakeLists.txt must be the project entry file, not a component CMakeLists; put idf_component_register/SRCS/REQUIRES only in main/CMakeLists.txt');
  }

  if (!mainCmake || !/idf_component_register\s*\(/.test(mainCmake.content || '')) {
    errors.push('main/CMakeLists.txt must use idf_component_register');
  }

  if (/register_component\s*\(|COMPONENT_SRCS|COMPONENT_REQUIRES|COMPONENT_ADD_INCLUDEDIRS/.test(mainCmake?.content || '')) {
    errors.push('old ESP-IDF component CMake style is not allowed; use idf_component_register');
  }

  const requires = cmakeRequires(mainCmake?.content || '');
  const forbiddenRequires = new Set([
    'esp_log',
    'esp_err',
    'sdkconfig',
    'FreeRTOS',
    'freertos/FreeRTOS.h',
    'task',
    'freertos/task.h',
    'queue',
    'semphr'
  ]);
  const duplicateRequires = requires.filter((item, index) => requires.indexOf(item) !== index);
  for (const dep of duplicateRequires) {
    warnings.push(`duplicate dependency in main/CMakeLists.txt REQUIRES: ${dep}`);
  }
  for (const dep of requires) {
    if (forbiddenRequires.has(dep)) {
      errors.push(`main/CMakeLists.txt REQUIRES must not contain ${dep}; ESP-IDF/FreeRTOS standard headers are includes, not SDK component dependencies`);
    }
  }

  if (sdkSummary) {
    const knownProjectDeps = new Set([
      ...(sdkSummary.components || []).map((component) => component.name),
      ...(sdkSummary.bsp || [])
    ]);
    const knownIdfDeps = new Set([
      'freertos',
      'esp_timer',
      'esp_wifi',
      'esp_event',
      'esp_netif',
      'nvs_flash',
      'driver',
      'esp_driver_gpio',
      'esp_driver_i2c',
      'esp_driver_uart',
      'esp_driver_ledc',
      'esp_driver_pcnt',
      'esp_hal_i2c',
      'esp_hal_ledc'
    ]);
    for (const dep of requires) {
      if (!knownProjectDeps.has(dep) && !knownIdfDeps.has(dep) && !forbiddenRequires.has(dep)) {
        errors.push(`main/CMakeLists.txt REQUIRES contains unknown component ${dep}; use scanned components/BSP names, not header names`);
      }
    }

    const board = selectedBoard(sdkSummary, boardName);
    const boardDefines = board?.defines || {};
    const usedComponents = expandComponentDeps([
      ...requires.filter((dep) => componentByName(sdkSummary, dep)),
      ...mentionedSdkComponents(`${content}\n${mainCmake?.content || ''}`)
    ], sdkSummary);
    for (const componentName of usedComponents) {
      const component = componentByName(sdkSummary, componentName);
      const missingMacros = (component?.requiredBoardMacros || [])
        .filter((macro) => !Object.prototype.hasOwnProperty.call(boardDefines, macro));
      if (missingMacros.length > 0) {
        errors.push(`component ${componentName} requires undefined board macros for ${board?.name || 'selected board'}: ${missingMacros.join(', ')}; do not use this component until board.h defines them`);
      }
    }
  }

  if (/#include\s+"bsp_board\.h"/.test(content)) {
    errors.push('bsp_board.h does not exist; use board.h only when BOARD_* macros are needed');
  }

  if (sdkSummary) {
    const standardQuotedHeaders = [
      'esp_err.h',
      'esp_log.h',
      'esp_system.h',
      'esp_timer.h',
      'esp_wifi.h',
      'nvs_flash.h',
      'sdkconfig.h',
      'freertos/FreeRTOS.h',
      'freertos/task.h',
      'freertos/queue.h',
      'freertos/event_groups.h',
      'freertos/semphr.h'
    ];
    const allowedQuotedHeaders = new Set([
      'board.h',
      ...standardQuotedHeaders,
      ...(sdkSummary.bsp || []).map((name) => `${name}.h`),
      ...(sdkSummary.components || []).flatMap((component) => (component.api || [])
        .map((api) => String(api.header || '').split('/').pop())
        .filter(Boolean)),
      ...generatedHeaderNames
    ]);
    const quotedIncludes = [...content.matchAll(/#include\s+"([^"]+)"/g)].map((match) => match[1]);
    for (const includePath of quotedIncludes) {
      const candidates = [includePath, includePath.split('/').pop()].filter(Boolean);
      if (!candidates.some((header) => allowedQuotedHeaders.has(header))) {
        errors.push(`quoted include ${includePath} was not found in scanned components/BSP headers, ESP-IDF standard headers, or generated project headers`);
      }
    }
  }

  if (/#include\s+"board\.h"/.test(content) && !/\bbsp_board\b/.test(mainCmake?.content || '')) {
    errors.push('including board.h requires bsp_board in main/CMakeLists.txt REQUIRES');
  }

  const componentHeaders = [
    ['driver_led.h', 'driver_led'],
    ['driver_button.h', 'driver_button'],
    ['driver_oled.h', 'driver_oled'],
    ['driver_servo.h', 'driver_servo'],
    ['driver_mpu6050.h', 'driver_mpu6050'],
    ['driver_tof2000c_vl53l0x.h', 'driver_tof2000c_vl53l0x'],
    ['driver_tw_tts.h', 'driver_tw_tts'],
    ['service_device.h', 'service_device'],
    ['service_pid.h', 'service_pid'],
    ['service_wifi.h', 'service_wifi'],
    ['service_physical_agent.h', 'service_physical_agent']
  ];
  for (const [header, component] of componentHeaders) {
    if (new RegExp(`#include\\s+"${header.replace('.', '\\.')}"`).test(content)
      && !new RegExp(`\\b${component}\\b`).test(mainCmake?.content || '')) {
      errors.push(`including ${header} requires ${component} in main/CMakeLists.txt REQUIRES`);
    }
  }

  if (sdkSummary) {
    for (const component of sdkSummary.components || []) {
      const functions = [...new Set((component.api || []).flatMap((api) => api.functions || []))];
      const componentUsed = functions.some((name) => new RegExp(`\\b${name}\\s*\\(`).test(content));
      if (!componentUsed) {
        continue;
      }
      if (!new RegExp(`\\b${component.name}\\b`).test(mainCmake?.content || '')) {
        errors.push(`calling ${component.name} API requires ${component.name} in main/CMakeLists.txt REQUIRES`);
      }
      const publicHeaders = (component.api || [])
        .map((api) => String(api.header || '').split('/').pop())
        .filter(Boolean);
      if (publicHeaders.length > 0 && !publicHeaders.some((header) => new RegExp(`#include\\s+"${header.replace('.', '\\.')}"`).test(content))) {
        errors.push(`calling ${component.name} API requires including one of: ${publicHeaders.join(', ')}`);
      }
    }
  }

  if (/\bservice_device_init\s*\(/.test(content) && !/#include\s+"service_device\.h"/.test(content)) {
    errors.push('service_device_init requires #include "service_device.h"');
  }

  if (/\bservice_device_init\s*\(/.test(content) && !/\bservice_device\b/.test(mainCmake?.content || '')) {
    errors.push('service_device_init requires service_device in main/CMakeLists.txt REQUIRES');
  }

  if (/esp_err_t\s+\w+\s*=\s*service_device_init\s*\(/.test(content)) {
    errors.push('service_device_init returns void, not esp_err_t');
  }

  if (/\.\s*(?:tx_pin|rx_pin)\b/.test(content)) {
    errors.push('bsp_uart_config_t fields are tx_gpio/rx_gpio, not tx_pin/rx_pin; use scanned struct fields exactly');
  }

  if ((/\bstatic\s+const\s+char\s*\*\s*TAG\b/.test(content) || /#define\s+TAG\b/.test(content))
    && !/\bESP_LOG[EWIDV]\s*\(\s*TAG\b/.test(content)) {
    errors.push('TAG is declared but never used by ESP_LOG*; add a meaningful ESP_LOG*(TAG, ...) call or remove TAG/esp_log.h');
  }

  for (const risk of snprintfTruncationRisks(content)) {
    errors.push(`snprintf into ${risk.dest}[${risk.destSize}] uses unbounded %s from ${risk.source}[${risk.sourceSize}]; use a precision such as %.Ns or increase the destination buffer`);
  }

  if (/driver_oled_init_profile\s*\(\s*\)/.test(content)) {
    errors.push('driver_oled_init_profile requires a driver_oled_profile_t argument');
  }

  if (/\bbutton_event_t\b/.test(content) || /\bBUTTON_(?:PRESSED|RELEASED|LONG_PRESS|SHORT_PRESS)\b/.test(content)) {
    errors.push('button driver uses driver_button_event_t and DRIVER_BUTTON_EVENT_* enum names');
  }

  if (/\bDRIVER_BUTTON_EVENT_(?:PRESSED|RELEASED)\b/.test(content)) {
    errors.push('button enum values are DRIVER_BUTTON_EVENT_PRESS and DRIVER_BUTTON_EVENT_RELEASE');
  }

  if (/\bDRIVER_OLED_PROFILE_128X64\b/.test(content)) {
    errors.push('OLED profile enum must be DRIVER_OLED_PROFILE_096_SSD1306 or DRIVER_OLED_PROFILE_13_SH1106');
  }

  if (/\bDRIVER_SERVO_ID_/.test(content)) {
    errors.push('servo enum values are DRIVER_SERVO_0 and DRIVER_SERVO_1');
  }

  if (/driver_tof2000c_vl53l0x_read_single\s*\(\s*&\s*(?:distance|dist|range_mm)\b/.test(content)) {
    errors.push('driver_tof2000c_vl53l0x_read_single expects driver_tof2000c_vl53l0x_result_t *');
  }

  if (/\btof_result\.status\b|\brange\.status\b/.test(content)) {
    errors.push('ToF result field is range_status, not status');
  }

  for (const file of files || []) {
    const filePath = String(file.path || '');
    if (!filePath.startsWith('project/') && !filePath.startsWith('physical_agent_config/config/')) {
      errors.push(`refusing unsupported output path: ${file.path}`);
    }
  }

  if (content.includes('service_wifi') && !content.includes('service_wifi_init_sta')) {
    warnings.push('WiFi appears in code; verify credentials/config flow before flashing');
  }

  return { ok: errors.length === 0, errors, warnings };
}
