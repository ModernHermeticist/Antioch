using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class GameManager : MonoBehaviour
{
    public static GameManager instance = null;

    public Grid grid;

    public Player player;

    public Enemy enemy;


	// Use this for initialization
	void Awake ()
    {
		if (instance == null)
        {
            instance = this;
        }

        else if (instance != this)
        {
            Destroy(gameObject);
        }

        DontDestroyOnLoad(gameObject);

        Instantiate(grid);

        Instantiate(player);

        Instantiate(enemy);
	}
	
	// Update is called once per frame
	void Update ()
    {
		
	}
}
