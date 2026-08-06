CREATE TABLE usage (
    id TEXT PRIMARY KEY,
    message_id TEXT NOT NULL,
    conversation_id TEXT NOT NULL,
    model_identifier TEXT NOT NULL,
    input_tokens INTEGER,
    output_tokens INTEGER,
    reasoning_tokens INTEGER,
    cache_creation_tokens INTEGER,
    cache_read_tokens INTEGER,
    total_tokens INTEGER,
    duration_ms INTEGER,
    ollama_total_duration_ns INTEGER,
    ollama_load_duration_ns INTEGER,
    ollama_prompt_eval_duration_ns INTEGER,
    ollama_eval_duration_ns INTEGER,
    estimated_cost_usd REAL,
    created_at TIMESTAMP NOT NULL,
    FOREIGN KEY (message_id) REFERENCES messages(id) ON DELETE CASCADE
);

CREATE UNIQUE INDEX idx_usage_message_id ON usage(message_id);
CREATE INDEX idx_usage_conversation_id ON usage(conversation_id);
