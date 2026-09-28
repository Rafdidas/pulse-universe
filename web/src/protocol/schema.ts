import { z } from 'zod';

// 계약서 4.3~4.5 절. 필드 이름은 엔진의 core/Snapshot.h 및
// network/Serializer.cpp 와 철자까지 같아야 한다.
export const PROTOCOL_VERSION = 1;

// null(모름)과 0(측정된 0)은 다른 값이다. 계약서 6.1 절.
const NullableNumber = z.number().nullable();

export const ChildProcessSchema = z.object({
  pid: z.number(),
  name: z.string(),
  role: z.string(),
  cpu_pct: NullableNumber,
  mem_mb: z.number(),
  threads: z.number(),
});

export const ProcessGroupSchema = z.object({
  key: z.string(),
  name: z.string(),
  root_pid: z.number(),
  cpu_pct: NullableNumber,
  mem_mb: z.number(),
  proc_count: z.number(),
  thread_count: z.number(),
  started_at: z.number(),
  account: z.enum(['user', 'system']),
  image_path: z.string(),
  children: z.array(ChildProcessSchema),
});

export const CoreLoadSchema = z.object({
  id: z.number(),
  pct: z.number(),
});

export const FlowSchema = z.object({
  group: z.string(),
  core: z.number(),
  weight: z.number(),
  source: z.enum(['estimated', 'measured']),
});

export const SpawnedProcessSchema = z.object({
  pid: z.number(),
  ppid: z.number(),
  name: z.string(),
  group: z.string(),
});

export const LifecycleSchema = z.object({
  spawned: z.array(SpawnedProcessSchema),
  terminated: z.array(z.number()),
});

export const SystemTotalsSchema = z.object({
  cpu_pct: NullableNumber,
  mem_used_mb: z.number(),
  mem_total_mb: z.number(),
  process_total: z.number(),
  thread_total: z.number(),
});

export const AmbientSchema = z.object({
  service_proc_count: z.number(),
  service_mem_mb: z.number(),
});

export const SnapshotSchema = z.object({
  type: z.literal('snapshot'),
  v: z.literal(PROTOCOL_VERSION),
  seq: z.number(),
  t: z.number(),
  system: SystemTotalsSchema,
  cores: z.array(CoreLoadSchema),
  groups: z.array(ProcessGroupSchema),
  flows: z.array(FlowSchema),
  lifecycle: LifecycleSchema,
  ambient: AmbientSchema,
});

export const HelloSchema = z.object({
  type: z.literal('hello'),
  v: z.literal(PROTOCOL_VERSION),
  interval_ms: z.number(),
  core_count: z.number(),
  capabilities: z.object({
    thread_mapping: z.enum(['estimated', 'measured']),
  }),
  host: z.object({
    os: z.string(),
    elevated: z.boolean(),
  }),
  session: z.string(),
});

export const MessageSchema = z.discriminatedUnion('type', [HelloSchema, SnapshotSchema]);

export type ChildProcess = z.infer<typeof ChildProcessSchema>;
export type ProcessGroup = z.infer<typeof ProcessGroupSchema>;
export type CoreLoad = z.infer<typeof CoreLoadSchema>;
export type Flow = z.infer<typeof FlowSchema>;
export type SpawnedProcess = z.infer<typeof SpawnedProcessSchema>;
export type Lifecycle = z.infer<typeof LifecycleSchema>;
export type SystemTotals = z.infer<typeof SystemTotalsSchema>;
export type Ambient = z.infer<typeof AmbientSchema>;
export type Snapshot = z.infer<typeof SnapshotSchema>;
export type Hello = z.infer<typeof HelloSchema>;
export type Message = z.infer<typeof MessageSchema>;

export type ParseOutcome =
  | { kind: 'message'; message: Message }
  | { kind: 'version-mismatch'; received: number }
  | { kind: 'invalid'; reason: string };

// 봉투의 v 를 먼저 본다. 버전이 다르면 나머지 모양을 따질 이유가 없고,
// 재연결해도 같은 버전이 오므로 호출자가 다르게 처리해야 한다 — 계약서 7.2 절.
const EnvelopeSchema = z.object({
  type: z.string(),
  v: z.number(),
});

export function parseMessage(text: string): ParseOutcome {
  let json: unknown;
  try {
    json = JSON.parse(text);
  } catch {
    return { kind: 'invalid', reason: 'not json' };
  }

  const envelope = EnvelopeSchema.safeParse(json);
  if (!envelope.success) {
    return { kind: 'invalid', reason: 'missing type or v' };
  }
  if (envelope.data.v !== PROTOCOL_VERSION) {
    return { kind: 'version-mismatch', received: envelope.data.v };
  }

  const parsed = MessageSchema.safeParse(json);
  if (!parsed.success) {
    return { kind: 'invalid', reason: parsed.error.issues[0]?.message ?? 'schema mismatch' };
  }
  return { kind: 'message', message: parsed.data };
}
