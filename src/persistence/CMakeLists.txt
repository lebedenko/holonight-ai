add_library(holonight_persistence STATIC
    include/holonight_persistence/conversation_record.h
    include/holonight_persistence/conversation_repository.h
    include/holonight_persistence/sqlite_conversation_repository.h
    include/holonight_persistence/detail/conversation_repository_worker.h
    include/holonight_persistence/migration_runner.h
    include/holonight_persistence/database_path.h
    include/holonight_persistence/usage_record.h
    src/conversation_record.cpp
    src/conversation_repository.cpp
    src/sqlite_conversation_repository.cpp
    src/detail/conversation_repository_worker.cpp
    src/migration_runner.cpp
    src/database_path.cpp
    src/usage_record.cpp
)

target_include_directories(holonight_persistence PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(holonight_persistence PUBLIC
    holonight_domain
    holonight_config
    Qt6::Core
    Qt6::Sql
)

target_compile_features(holonight_persistence PUBLIC cxx_std_23)

qt_add_resources(holonight_persistence "holonight_persistence_migrations"
    PREFIX "/holonight_persistence"
    FILES
        migrations/0001_init.sql
        migrations/0002_add_message_model_id.sql
        migrations/0003_add_title_source.sql
        migrations/0004_add_usage.sql
        migrations/0005_add_usage_cost_breakdown.sql
        migrations/0006_drop_usage_cost_columns.sql
        migrations/0007_add_pinned_at.sql
        migrations/0008_add_message_tool_calls.sql
)
