using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class Player : MonoBehaviour 
{
    [SerializeField]
    private Stat health;



	private float speed = 1.5f;
    private float jump = 50.0f;

    private bool canAttack = false;
    private float damage = 1.0f;

    private bool isJumping = false;
    private bool canJump = true;
    private Vector3 direction = new Vector3(0, 0, 0);

    private GameObject target;



    void Awake () 
	{
        name = "Player";
        health.Initialize();
	}

    // Update is called once per frame
    void Update () 
	{
        if (Input.GetKeyDown(KeyCode.Q))
        {
            health.CurrentVal -= 10;
        }
        if (Input.GetKeyDown(KeyCode.E))
        {
            health.CurrentVal += 10;
        }


        if (isJumping == true)
        {
            StartCoroutine(JumpDelayTimer());
            isJumping = false;
        }
        Vector3 velocity = new Vector3(0,0,0);
       
        if(Input.GetMouseButtonDown(0) && canAttack)
        {
            target.GetComponent<Enemy>().TakeDamage(damage);
        }
        if (Input.GetKey(KeyCode.Space))
        {
            if (canJump == true)
            {
                isJumping = true;
                canJump = false;
            }
        }

        if (Input.GetKey("w"))
        {
            velocity.y += speed * Time.deltaTime;
            direction.y = 1;
        }

        if (Input.GetKey("s"))
		{
			velocity.y -= speed * Time.deltaTime;
            direction.y = -1;
		}

		if (Input.GetKey("d"))
		{
			velocity.x += speed * Time.deltaTime;
            direction.x = 1;
		}

		if (Input.GetKey("a"))
		{
			velocity.x -= speed * Time.deltaTime;
            direction.x = -1;
		}

		if (velocity.magnitude > 0)
		{
            if (isJumping)
            {
                velocity = velocity.normalized * speed * jump;
            }
            else
            {
                velocity = velocity.normalized * speed;
            }
		}

        if (direction.magnitude > 0)
        {
            direction = direction.normalized;
        }
		transform.position += velocity * Time.deltaTime;

    }

	void OnCollisionEnter2D(Collision2D col)
	{
		//Debug.Log("OnCollisionEnter2D");
	}

	void OnCollisionExit2D(Collision2D col)
	{
		//Debug.Log("OnCollisionExit2D");
	}

    public IEnumerator JumpDelayTimer()
    {
        yield return new WaitForSeconds(3); // Wait for 3 seconds
        canJump = true;
    }

    public void SetCanAttack()
    {
        if (canAttack == false)
        {
            canAttack = true;
        }
        else if (canAttack == true)
        {
            canAttack = false;
        }
    }

    public void SetTarget(GameObject target)
    {
        this.target = target;
    }

}
