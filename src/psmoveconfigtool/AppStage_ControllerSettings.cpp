//-- inludes -----
#include "AppStage_ControllerSettings.h"
#include "AppStage_AccelerometerCalibration.h"
#include "AppStage_GyroscopeCalibration.h"
#include "AppStage_MagnetometerCalibration.h"
#include "AppStage_MainMenu.h"
#include "AppStage_PairController.h"
#include "AppStage_TestRumble.h"
#include "App.h"
#include "Camera.h"
#include "MathUtility.h"
#include "Renderer.h"
#include "UIConstants.h"
#include "PSMoveProtocolInterface.h"
#include "PSMoveProtocol.pb.h"

#include "SDL_keycode.h"

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <sstream>

#ifdef _MSC_VER
#pragma warning (disable: 4996) // 'This function or variable may be unsafe': snprintf
#define snprintf _snprintf
#endif

//-- statics ----
const char *AppStage_ControllerSettings::APP_STAGE_NAME= "ControllerSettings";

//-- constants -----

//-- public methods -----
AppStage_ControllerSettings::AppStage_ControllerSettings(App *app) 
    : AppStage(app)
    , m_menuState(AppStage_ControllerSettings::inactive)
    , m_selectedControllerIndex(-1)
{ }

void AppStage_ControllerSettings::enter()
{
    m_app->setCameraType(_cameraFixed);

    request_controller_list();
}

void AppStage_ControllerSettings::exit()
{
    m_menuState= AppStage_ControllerSettings::inactive;
}

void AppStage_ControllerSettings::update()
{
}
    
void AppStage_ControllerSettings::render()
{
    glm::mat4 scale2RotateX90= 
        glm::rotate(
            glm::scale(glm::mat4(1.f), glm::vec3(2.f, 2.f, 2.f)), 
            90.f, glm::vec3(1.f, 0.f, 0.f));    

    switch (m_menuState)
    {
    case eControllerMenuState::idle:
        {
            if (m_selectedControllerIndex >= 0)
            {
                const ControllerInfo &controllerInfo= m_bluetoothControllerInfos[m_selectedControllerIndex];

                switch(controllerInfo.ControllerType)
                {
                    case ClientControllerView::eControllerType::PSMove:
                    case ClientControllerView::eControllerType::PSDualShock4:
                        {
                            const ControllerInfo &controllerInfo = m_bluetoothControllerInfos[m_selectedControllerIndex];

                            // Display the tracking color being used for the controller
                            glm::vec3 bulb_color = glm::vec3(1.f, 1.f, 1.f);

                            switch (controllerInfo.TrackingColorType)
                            {
                            case PSMoveTrackingColorType::Magenta:
                                bulb_color = glm::vec3(1.f, 0.f, 1.f);
                                break;
                            case PSMoveTrackingColorType::Cyan:
                                bulb_color = glm::vec3(0.f, 1.f, 1.f);
                                break;
                            case PSMoveTrackingColorType::Yellow:
                                bulb_color = glm::vec3(1.f, 1.f, 0.f);
                                break;
                            case PSMoveTrackingColorType::Red:
                                bulb_color = glm::vec3(1.f, 0.f, 0.f);
                                break;
                            case PSMoveTrackingColorType::Green:
                                bulb_color = glm::vec3(0.f, 1.f, 0.f);
                                break;
                            case PSMoveTrackingColorType::Blue:
                                bulb_color = glm::vec3(0.f, 0.f, 1.f);
                                break;
                            default:
                                break;
                            }

                            if (controllerInfo.ControllerType == ClientControllerView::PSMove)
                            {
                                drawPSMoveModel(scale2RotateX90, bulb_color);
                            }
                            else
                            {
                                drawPSDualShock4Model(scale2RotateX90, bulb_color);
                            }
                        } break;
                    case ClientControllerView::eControllerType::PSNavi:
                        {
                            drawPSNaviModel(scale2RotateX90);
                        } break;
                    default:
                        assert(0 && "Unreachable");
                }        
            }
        } break;

    case eControllerMenuState::pendingControllerListRequest:
    case eControllerMenuState::failedControllerListRequest:
        {
        } break;

    default:
        assert(0 && "unreachable");
    }
}

void AppStage_ControllerSettings::renderUI()
{
    const char *k_window_title= "Controller Settings";
    const ImGuiWindowFlags window_flags = 
        ImGuiWindowFlags_ShowBorders |
        ImGuiWindowFlags_NoResize | 
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoCollapse;

    switch (m_menuState)
    {
    case eControllerMenuState::idle:
        {
            ImGui::SetNextWindowPosCenter();
            ImGui::SetNextWindowSize(ImVec2(350, 440));
            ImGui::Begin(k_window_title, nullptr, window_flags);

            if (m_hostSerial.length() > 1 && m_hostSerial != "00:00:00:00:00:00")
            {
                ImGui::Text("Host Serial: %s", m_hostSerial.c_str());
            }
            else
            {
                ImGui::Text("No bluetooth adapter detected!");
            }
            
            ImGui::Separator();

            if (m_bluetoothControllerInfos.size() > 0)
            {
                ControllerInfo &controllerInfo= m_bluetoothControllerInfos[m_selectedControllerIndex];

                if (m_selectedControllerIndex > 0)
                {
                    if (ImGui::Button("<##ControllerIndex"))
                    {
                        --m_selectedControllerIndex;
                    }
                    ImGui::SameLine();
                }
                ImGui::Text("Controller: %d", m_selectedControllerIndex);
                if (m_selectedControllerIndex + 1 < static_cast<int>(m_bluetoothControllerInfos.size()))
                {
                    ImGui::SameLine();
                    if (ImGui::Button(">##ControllerIndex"))
                    {
                        ++m_selectedControllerIndex;
                    }
                }

				// Combo box selection for controller tracking color
				if (controllerInfo.ControllerType != ClientControllerView::PSNavi)
				{
					int newTrackingColorType = controllerInfo.TrackingColorType;

					if (ImGui::Combo("Tracking Color", &newTrackingColorType, "Magenta\0Cyan\0Yellow\0Red\0Green\0Blue\0\0"))
					{
						controllerInfo.TrackingColorType = static_cast<PSMoveTrackingColorType>(newTrackingColorType);

						request_set_controller_tracking_color_id(controllerInfo.ControllerID, controllerInfo.TrackingColorType);

						// Re-request the controller list since the tracking colors could changed for other controllers
						request_controller_list();
					}
				}

                ImGui::BulletText("Controller ID: %d", controllerInfo.ControllerID);

                switch(controllerInfo.ControllerType)
                {
                    case ClientControllerView::eControllerType::PSMove:
                        {
                            ImGui::BulletText("Controller Type: PSMove");
                        } break;
                    case ClientControllerView::eControllerType::PSNavi:
                        {
                            ImGui::BulletText("Controller Type: PSNavi");
                        } break;
                    case ClientControllerView::eControllerType::PSDualShock4:
                        {
                            ImGui::BulletText("Controller Type: PSDualShock4");
                        } break;
                    default:
                        assert(0 && "Unreachable");
                }

                ImGui::BulletText("Device Serial: %s", controllerInfo.DeviceSerial.c_str());
                ImGui::BulletText("Assigned Host Serial: %s", controllerInfo.AssignedHostSerial.c_str());
                ImGui::BulletText("Device Path:");
                ImGui::SameLine();
                ImGui::TextWrapped("%s", controllerInfo.DevicePath.c_str());

                if (controllerInfo.ControllerType == ClientControllerView::eControllerType::PSMove)
                {
                    if (ImGui::Button("Calibrate Magnetometer"))
                    {
                        m_app->getAppStage<AppStage_MagnetometerCalibration>()->setBypassCalibrationFlag(false);
                        m_app->setAppStage(AppStage_MagnetometerCalibration::APP_STAGE_NAME);
                    }

                    if (ImGui::Button("Calibrate Gyroscope"))
                    {
                        m_app->getAppStage<AppStage_GyroscopeCalibration>()->setBypassCalibrationFlag(false);
                        m_app->setAppStage(AppStage_GyroscopeCalibration::APP_STAGE_NAME);
                    }

                    if (ImGui::Button("Test Orientation"))
                    {
                        m_app->getAppStage<AppStage_MagnetometerCalibration>()->setBypassCalibrationFlag(true);
                        m_app->setAppStage(AppStage_MagnetometerCalibration::APP_STAGE_NAME);
                    }
                }

                if (controllerInfo.ControllerType == ClientControllerView::eControllerType::PSDualShock4)
                {
                    if (ImGui::Button("Calibrate Gyroscope"))
                    {
                        m_app->getAppStage<AppStage_GyroscopeCalibration>()->setBypassCalibrationFlag(false);
                        m_app->setAppStage(AppStage_GyroscopeCalibration::APP_STAGE_NAME);
                    }

                    if (ImGui::Button("Test Orientation"))
                    {
                        m_app->getAppStage<AppStage_GyroscopeCalibration>()->setBypassCalibrationFlag(true);
                        m_app->setAppStage(AppStage_GyroscopeCalibration::APP_STAGE_NAME);
                    }
                }

                if (controllerInfo.ControllerType == ClientControllerView::eControllerType::PSMove || 
                    controllerInfo.ControllerType == ClientControllerView::eControllerType::PSDualShock4)
                {
                    if (ImGui::Button("Calibrate Accelerometer"))
                    {
                        m_app->getAppStage<AppStage_AccelerometerCalibration>()->setBypassCalibrationFlag(false);
                        m_app->setAppStage(AppStage_AccelerometerCalibration::APP_STAGE_NAME);
                    }

                    if (ImGui::Button("Test Accelerometer"))
                    {
                        m_app->getAppStage<AppStage_AccelerometerCalibration>()->setBypassCalibrationFlag(true);
                        m_app->setAppStage(AppStage_AccelerometerCalibration::APP_STAGE_NAME);
                    }

                    if (ImGui::Button("Test Rumble"))
                    {
                        m_app->setAppStage(AppStage_TestRumble::APP_STAGE_NAME);
                    }
                }
            }
            else
            {
                ImGui::Text("No Bluetooth connected controllers");
            }

            ImGui::Separator();

            // If there are any controllers waiting to be paired, 
            // just present the first one as an option
            if (m_usbControllerInfos.size() > 0)
            {
                // Only consider the first controller connected via usb
                ControllerInfo &controllerInfo = m_usbControllerInfos[0];

                // We can only unpair controllers connected via usb
                if (controllerInfo.PairedToHost)
                {
                    if (ImGui::Button("Unpair USB Controller"))
                    {
                        m_app->getAppStage<AppStage_PairController>()->request_controller_unpair(controllerInfo.ControllerID);
                        m_app->setAppStage(AppStage_PairController::APP_STAGE_NAME);
                    }
                }
                else
                {
                    if (ImGui::Button("Pair USB Controller"))
                    {
                        m_app->getAppStage<AppStage_PairController>()->request_controller_pair(controllerInfo.ControllerID);
                        m_app->setAppStage(AppStage_PairController::APP_STAGE_NAME);
                    }
                }
            }
            else
            {
                ImGui::Text("No USB connected controllers");
            }

            ImGui::Separator();

            if (ImGui::Button("Return to Main Menu"))
            {
                m_app->setAppStage(AppStage_MainMenu::APP_STAGE_NAME);
            }

            ImGui::End();
        } break;
    case eControllerMenuState::pendingControllerListRequest:
        {
            ImGui::SetNextWindowPosCenter();
            ImGui::SetNextWindowSize(ImVec2(300, 150));
            ImGui::Begin(k_window_title, nullptr, window_flags);

            ImGui::Text("Waiting for controller list response...");

            ImGui::End();
        } break;
    case eControllerMenuState::failedControllerListRequest:
        {
            ImGui::SetNextWindowPosCenter();
            ImGui::SetNextWindowSize(ImVec2(300, 150));
            ImGui::Begin(k_window_title, nullptr, window_flags);

            ImGui::Text("Failed to get controller list!");

            if (ImGui::Button("Retry"))
            {
                request_controller_list();
            }

            if (ImGui::Button("Return to Main Menu"))
            {
                m_app->setAppStage(AppStage_MainMenu::APP_STAGE_NAME);
            }

            ImGui::End();
        } break;

    default:
        assert(0 && "unreachable");
    }
}

bool AppStage_ControllerSettings::onClientAPIEvent(
    ClientPSMoveAPI::eEventType event, 
    ClientPSMoveAPI::t_event_data_handle opaque_event_handle)
{
    bool bHandled= false;

    switch(event)
    {
    case ClientPSMoveAPI::controllerListUpdated:
        {
            bHandled= true;
            request_controller_list();
        } break;
    }

    return bHandled;
}

void AppStage_ControllerSettings::request_controller_list()
{
    if (m_menuState != AppStage_ControllerSettings::pendingControllerListRequest)
    {
        m_menuState= AppStage_ControllerSettings::pendingControllerListRequest;

        // Tell the psmove service that we we want a list of controllers connected to this machine
        RequestPtr request(new PSMoveProtocol::Request());
        request->set_type(PSMoveProtocol::Request_RequestType_GET_CONTROLLER_LIST);

        // Do get controllers connected bia USB in this menu since we need the info for pairing/unpairing
        request->mutable_request_get_controller_list()->set_include_usb_controllers(true);

        ClientPSMoveAPI::register_callback(
            ClientPSMoveAPI::send_opaque_request(&request), 
            AppStage_ControllerSettings::handle_controller_list_response, this);
    }
}

void AppStage_ControllerSettings::handle_controller_list_response(
    const ClientPSMoveAPI::ResponseMessage *response_message,
    void *userdata)
{
    AppStage_ControllerSettings *thisPtr= static_cast<AppStage_ControllerSettings *>(userdata);

    const ClientPSMoveAPI::eClientPSMoveResultCode ResultCode = response_message->result_code;
    const ClientPSMoveAPI::t_response_handle response_handle = response_message->opaque_response_handle;

    switch(ResultCode)
    {
        case ClientPSMoveAPI::_clientPSMoveResultCode_ok:
        {
            const PSMoveProtocol::Response *response= GET_PSMOVEPROTOCOL_RESPONSE(response_handle);
			int oldSelectedControllerIndex= thisPtr->m_selectedControllerIndex;

            thisPtr->m_hostSerial = response->result_controller_list().host_serial();
			thisPtr->m_selectedControllerIndex= -1;
			thisPtr->m_bluetoothControllerInfos.clear();
			thisPtr->m_usbControllerInfos.clear();

            for (int controller_index= 0; controller_index < response->result_controller_list().controllers_size(); ++controller_index)
            {
                const auto &ControllerResponse= response->result_controller_list().controllers(controller_index);

                AppStage_ControllerSettings::ControllerInfo ControllerInfo;

                ControllerInfo.ControllerID= ControllerResponse.controller_id();

                switch(ControllerResponse.controller_type())
                {
                case PSMoveProtocol::PSMOVE:
                    ControllerInfo.ControllerType = ClientControllerView::eControllerType::PSMove;
                    break;
                case PSMoveProtocol::PSNAVI:
                    ControllerInfo.ControllerType = ClientControllerView::eControllerType::PSNavi;
                    break;
                case PSMoveProtocol::PSDUALSHOCK4:
                    ControllerInfo.ControllerType = ClientControllerView::eControllerType::PSDualShock4;
                    break;
                default:
                    assert(0 && "unreachable");
                }

                ControllerInfo.TrackingColorType = 
                    static_cast<PSMoveTrackingColorType>(ControllerResponse.tracking_color_type());
                ControllerInfo.DevicePath= ControllerResponse.device_path();
                ControllerInfo.DeviceSerial= ControllerResponse.device_serial();
                ControllerInfo.AssignedHostSerial= ControllerResponse.assigned_host_serial();
                ControllerInfo.PairedToHost=
                    ControllerResponse.assigned_host_serial().length() > 0 && 
                    ControllerResponse.assigned_host_serial() == thisPtr->m_hostSerial;

                // Add the controller to the appropriate connection list
                switch (ControllerResponse.connection_type())
                {
                case PSMoveProtocol::Response_ResultControllerList_ControllerInfo_ConnectionType_BLUETOOTH:
                    thisPtr->m_bluetoothControllerInfos.push_back(ControllerInfo);
                    break;
                case PSMoveProtocol::Response_ResultControllerList_ControllerInfo_ConnectionType_USB:
                    thisPtr->m_usbControllerInfos.push_back(ControllerInfo);
                    break;
                default:
                    assert(0 && "unreachable");
                }
            }

			if (oldSelectedControllerIndex != -1)
			{
				// Maintain the same position in the list if possible
				thisPtr->m_selectedControllerIndex= 
					(oldSelectedControllerIndex < static_cast<int>(thisPtr->m_bluetoothControllerInfos.size())) 
					? oldSelectedControllerIndex
					: 0;
			}
			else
			{
	            thisPtr->m_selectedControllerIndex= (thisPtr->m_bluetoothControllerInfos.size() > 0) ? 0 : -1;
			}

            thisPtr->m_menuState= AppStage_ControllerSettings::idle;
        } break;

        case ClientPSMoveAPI::_clientPSMoveResultCode_error:
        case ClientPSMoveAPI::_clientPSMoveResultCode_canceled:
        { 
            thisPtr->m_menuState= AppStage_ControllerSettings::failedControllerListRequest;
        } break;
    }
}

void AppStage_ControllerSettings::request_set_controller_tracking_color_id(
	int ControllerID,
	PSMoveTrackingColorType tracking_color_type)
{
	RequestPtr request(new PSMoveProtocol::Request());
	request->set_type(PSMoveProtocol::Request_RequestType_SET_LED_TRACKING_COLOR);
	request->mutable_set_led_tracking_color_request()->set_controller_id(ControllerID);
	request->mutable_set_led_tracking_color_request()->set_color_type(
		static_cast<PSMoveProtocol::TrackingColorType>(tracking_color_type));

	ClientPSMoveAPI::eat_response(ClientPSMoveAPI::send_opaque_request(&request));
}