-- 创建报警处理记录表
CREATE TABLE IF NOT EXISTS alarm_handlings (
    id INT PRIMARY KEY AUTO_INCREMENT COMMENT '记录ID',
    handle_time DATETIME NOT NULL COMMENT '处理时间',
    handler VARCHAR(50) NOT NULL COMMENT '处理人员',
    action TEXT NOT NULL COMMENT '处理方式',
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '记录创建时间',
    INDEX idx_handle_time (handle_time),
    INDEX idx_handler (handler)
) COMMENT '报警处理记录表';