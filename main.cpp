
#include <iostream>
#include <string>
#include <crow.h>
#include <nlohmann/json.hpp>
#include <mysqlx/xdevapi.h>

std::unique_ptr<mysqlx::Session> session = nullptr;

std::string getCurrentTime() {
    std::time_t now = std::time(nullptr);

    // Prepare a tm struct to hold the local time
    std::tm local_time;

    // Use localtime_s to convert time_t to tm (thread-safe)
    localtime_s(&local_time, &now);

    // Format the current time as a MySQL-compatible datetime string
    std::ostringstream oss;
    oss << 1900 + local_time.tm_year << "-"
        << 1 + local_time.tm_mon << "-"
        << local_time.tm_mday << " "
        << local_time.tm_hour << ":"
        << std::setw(2) << std::setfill('0') << local_time.tm_min << ":"
        << std::setw(2) << std::setfill('0') << local_time.tm_sec;

    return oss.str();
}

std::string raw_bytes_to_datetime(const std::string& raw_data) {
    if (raw_data.size() < 5) {
        return "Invalid Date";  // Ensure the raw_data has at least 5 bytes
    }

    // Extract the date components from the raw binary data
    unsigned int year = (unsigned char)raw_data[0] | ((unsigned char)raw_data[1] << 7);  // Year is 2 bytes (little-endian)
    unsigned int month = (unsigned char)raw_data[2];  // Month is 1 byte
    unsigned int day = (unsigned char)raw_data[3];    // Day is 1 byte
    unsigned int hour = (unsigned char)raw_data[4];   // Hour is 1 byte
    unsigned int minute = (unsigned char)raw_data[5]; // Minute is 1 byte
    unsigned int second = (unsigned char)raw_data[6]; // Second is 1 byte

    // Format the datetime into a human-readable string
    std::ostringstream oss;
    oss << std::setw(4) << std::setfill('0') << year << "-"
        << std::setw(2) << std::setfill('0') << month << "-"
        << std::setw(2) << std::setfill('0') << day << " "
        << std::setw(2) << std::setfill('0') << hour << ":"
        << std::setw(2) << std::setfill('0') << minute << ":"
        << std::setw(2) << std::setfill('0') << second;

    return oss.str();
}

std::string handle_date_field(const mysqlx::Value& date_value) {
    if (date_value.getType() == mysqlx::Value::RAW) {
        const std::string& raw_data = date_value.get<std::string>();
        return raw_bytes_to_datetime(raw_data);  // Convert raw bytes to a human-readable datetime string
    }
    else if (date_value.getType() == mysqlx::Value::STRING) {
        return date_value.get<std::string>();
    }
    else {
        return "Unknown Type";
    }
}



void connect() {
    if (!session) {
        try {
            // Connect to database
            session = std::make_unique<mysqlx::Session>("mysqlx://root:pass@127.0.0.1");
            std::cout << "Connected to the database" << std::endl;
        }
        catch (const mysqlx::Error& err) {
            std::cerr << "MySQL Error: " << err.what() << std::endl;
            throw;
        }
    }
}



int main()
{
    crow::SimpleApp app;

    // Test endpoint
    CROW_ROUTE(app, "/")
        ([]() {
        return "Hello world";
            });

    // Event creation end point
    CROW_ROUTE(app, "/event").methods(crow::HTTPMethod::Post)
        ([](const crow::request& req) {
        try {
            std::cout << "Received body: '" << req.body << "'" << std::endl;

            if (req.body.empty()) {
                std::cerr << "error: empty body received!" << std::endl;
                return crow::response(400, "empty request body");
            }

            auto body_data = nlohmann::json::parse(req.body);

            // Early return in case body does not have the necessary fields
            if (!((body_data.contains("event_name") && body_data.contains("location") && body_data.contains("date") && body_data.contains("max_tickets") && body_data.contains("type")))) {
                return crow::response(400, "JSON does not contain all necessary fields");
            }

            std::string event_name = body_data["event_name"];
            std::string location = body_data["location"];
            std::string date = body_data["date"];
            int max_tickets = body_data["max_tickets"];
            std::string type = body_data["type"];

            //Re-establish conection to the DB
            connect();

            mysqlx::Schema sch = session->getSchema("test");
            mysqlx::Table event_table = sch.getTable("event");

            event_table.insert("event_name", "location", "date", "max_tickets", "type")
                .values(event_name, location, date, max_tickets, type)
                .execute();
            return crow::response(200, "Event created successfully!");
        }
        catch (const mysqlx::Error& err) {
            std::cerr << "MySQL Error: " << err.what() << std::endl;
            return crow::response(500, "Database error");
        }
        catch (const std::exception& e) {
            std::cerr << "Error parsing JSON: " << e.what() << std::endl;
            return crow::response(400, "Invalid JSON format");
        }
            });

    CROW_ROUTE(app, "/event/current").methods(crow::HTTPMethod::Get)
        ([]() {
        std::string current_time = getCurrentTime();
        connect();

        try {
            mysqlx::Schema sch = session->getSchema("test");
            mysqlx::Table event_table = sch.getTable("event");
            //Get all current events
            mysqlx::RowResult result = event_table
                .select("event_name", "location", "date", "max_tickets", "type")
                .where("date > :current_time")
                .bind("current_time", current_time)
                .execute();

            nlohmann::json response_json = nlohmann::json::array();

            for (mysqlx::Row row : result) {
                nlohmann::json event;
                event["event_name"] = row[0].get<std::string>();
                event["location"] = row[1].get<std::string>();
                event["date"] = handle_date_field(row[2]);
                event["max_tickets"] = row[3].get<int>();
                event["type"] = row[4].get<std::string>();
                std::cout << event["type"] << std::endl;

                response_json.push_back(event);
            }
            std::cout << response_json << std::endl;
            return crow::response(200, response_json.dump());
        }
        catch (const mysqlx::Error& err) {
            std::cerr << "MySQL Error: " << err.what() << std::endl;
            return crow::response(500, "Database error");
        }
    });

    CROW_ROUTE(app, "/user").methods(crow::HTTPMethod::Post)
    ([](const crow::request& req){
        try {
            std::cout << "Received body: '" << req.body << "'" << std::endl;

            if (req.body.empty()) {
                std::cerr << "error: empty body received!" << std::endl;
                return crow::response(400, "empty request body");
            }

            auto body_data = nlohmann::json::parse(req.body);

            // Early return in case body does not have the necessary fields
            if (!(body_data.contains("name") && body_data.contains("rol") && body_data.contains("email") && body_data.contains("password"))) {
                return crow::response(400, "JSON does not contain all necessary fields");
            }

            std::string name = body_data["name"];
            std::string rol = body_data["rol"];
            std::string email = body_data["email"];
            std::string password = body_data["password"];

            //Re-establish conection to the DB
            connect();

            mysqlx::Schema sch = session->getSchema("test");
            mysqlx::Table user_table = sch.getTable("user");

            user_table.insert("name", "rol", "email", "password")
				.values(name, rol, email, password) // We should hash the password before storing it
                .execute();
            return crow::response(200, "User created successfully!");
        }
        catch (const mysqlx::Error& err) {
            std::cerr << "MySQL Error: " << err.what() << std::endl;
            return crow::response(500, "Database error");
        }
        catch (const std::exception& e) {
            std::cerr << "Error parsing JSON: " << e.what() << std::endl;
            return crow::response(400, "Invalid JSON format");
        }
    });

    CROW_ROUTE(app, "/ticket/").methods(crow::HTTPMethod::Post)
    ([](const crow::request & req) {
        try {

			crow::query_string query_params = req.url_params;

            int user_id = 0;
            int event_id = 0;
            if (query_params.get("user_id")) {
                user_id = std::stoi(query_params.get("user_id"));
            }
            else {
                return crow::response(400, "Missing user_id query parameter");
            }

            if (query_params.get("event_id")) {
                event_id = std::stoi(query_params.get("event_id"));
            }
            else {
                return crow::response(400, "Missing event_id query parameter");
            }

            std::cout << "Received POST request for user_id: " << user_id << " and event_id: " << event_id << std::endl;

			std::string date = getCurrentTime();


            //Re-establish conection to the DB
            connect();

            mysqlx::Schema sch = session->getSchema("test");
            mysqlx::Table event_table = sch.getTable("event");
            mysqlx::Table user_table = sch.getTable("user");
			mysqlx::Table ticket_table = sch.getTable("ticket");



			//Check if the user exists
			mysqlx::RowResult user_result = user_table.select("id").where("id = :id").bind("id", user_id).execute();
			if (user_result.count() == 0) {
				return crow::response(400, "User does not exist");
			}

			//Check if the event exists
			mysqlx::RowResult event_result = event_table.select("id", "max_tickets").where("id = :id").bind("id", event_id).execute();
			if (event_result.count() == 0) {
				return crow::response(400, "Event does not exist");
			}
            
            mysqlx::Row event_row = event_result.fetchOne();

            int max_tickets = event_row[1].get<int>();

			//Check if the event has tickets available
			mysqlx::RowResult ticket_result = ticket_table.select("id").where("event_id = :event_id").bind("event_id", event_id).execute();
            std::cout << "ticket table queried" << std::endl;
			if (max_tickets <= ticket_result.count()) {
				return crow::response(400, "No tickets available for this event");
			}

			ticket_table.insert("user_id", "event_id", "booking_date")
				.values(user_id, event_id, date)
				.execute();

            return crow::response(200, "ticket created successfully!");
        }
        catch (const mysqlx::Error& err) {
            std::cerr << "MySQL Error: " << err.what() << std::endl;
            return crow::response(500, "Database error");
        }
        catch (const std::exception& e) {
            std::cerr << "Error parsing JSON: " << e.what() << std::endl;
            return crow::response(400, "An error occurred: " + std::string(e.what()));
        }
    });

    CROW_ROUTE(app, "/ticket/").methods(crow::HTTPMethod::Get)
    ([](const crow::request& req) {
        
        crow::query_string query_params = req.url_params;

        int user_id = 0;
        int event_id = 0;
        if (query_params.get("user_id")) {
            user_id = std::stoi(query_params.get("user_id"));
        }
        else {
            return crow::response(400, "Missing user_id query parameter");
        }

        connect();

        try {

            mysqlx::Schema sch = session->getSchema("test");
            mysqlx::Table ticket_table = sch.getTable("ticket");
            mysqlx::Table user_table = sch.getTable("user");

            // We check if user exists
			mysqlx::RowResult user_result = user_table.select("id").where("id = :id").bind("id", user_id).execute();
			if (user_result.count() == 0) {
				return crow::response(400, "User does not exist");
			}

            //Get all tickets from user
            mysqlx::RowResult result = ticket_table
                .select("id", "event_id", "booking_date")
                .execute();

            nlohmann::json response_json = nlohmann::json::array();

            for (mysqlx::Row row : result) {
                nlohmann::json ticket;
				ticket["id"] = row[0].get<int>();
                ticket["event_id"] = row[1].get<int>();
                ticket["booking_date"] = handle_date_field(row[2]);
                response_json.push_back(ticket);
            }
            std::cout << response_json << std::endl;
            return crow::response(200, response_json.dump());
        }
        catch (const mysqlx::Error& err) {
            std::cerr << "MySQL Error: " << err.what() << std::endl;
            return crow::response(500, "Database error");
        }
    });

    CROW_ROUTE(app, "/event/").methods(crow::HTTPMethod::Get)
        ([](const crow::request& req) {

        crow::query_string query_params = req.url_params;

        int event_id = 0;
        if (query_params.get("event_id")) {
            event_id = std::stoi(query_params.get("event_id"));
        }
        else {
            return crow::response(400, "Missing event_id query parameter");
        }

        connect();

        try {

            mysqlx::Schema sch = session->getSchema("test");
            mysqlx::Table event_table = sch.getTable("event");

            // We get the event
            mysqlx::RowResult event_result = event_table.select("id","event_name","location","date","max_tickets", "type").where("id = :id").bind("id", event_id).execute();
            if (event_result.count() == 0) {
                return crow::response(400, "Event id does not exist");
            }

			mysqlx::Row event_row = event_result.fetchOne();

            nlohmann::json event;
			event["id"] = event_row[0].get<int>();
			event["event_name"] = event_row[1].get<std::string>();
			event["location"] = event_row[2].get<std::string>();
			event["date"] = handle_date_field(event_row[3]);
			event["max_tickets"] = event_row[4].get<int>();
			event["type"] = event_row[5].get<std::string>();

            std::cout << event << std::endl;
            return crow::response(200, event.dump());
        }
        catch (const mysqlx::Error& err) {
            std::cerr << "MySQL Error: " << err.what() << std::endl;
            return crow::response(500, "Database error");
        }
            });


    app.port(18080).multithreaded().run();
}
