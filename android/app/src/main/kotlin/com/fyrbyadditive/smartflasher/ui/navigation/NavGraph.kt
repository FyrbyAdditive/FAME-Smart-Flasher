package com.fyrbyadditive.smartflasher.ui.navigation

import androidx.compose.runtime.Composable
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import com.fyrbyadditive.smartflasher.ui.screens.AboutScreen
import com.fyrbyadditive.smartflasher.ui.screens.HomeScreen

/**
 * Navigation destinations
 */
sealed class Screen(val route: String) {
    object Home : Screen("home")
    object About : Screen("about")
}

/**
 * Navigation graph for the app
 */
@Composable
fun NavGraph(
    navController: NavHostController,
    startDestination: String = Screen.Home.route
) {
    NavHost(
        navController = navController,
        startDestination = startDestination
    ) {
        composable(Screen.Home.route) {
            HomeScreen(
                onNavigateToAbout = {
                    navController.navigate(Screen.About.route)
                }
            )
        }

        composable(Screen.About.route) {
            AboutScreen(
                onBack = {
                    navController.popBackStack()
                }
            )
        }
    }
}
